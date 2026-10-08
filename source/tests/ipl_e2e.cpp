// FLASHDUMP-1 (2026-10-08): the whole flash-dump route end to end on the EMULATED MS2000R, before any real unit sees it.
//   ipl_e2e <flash.bin> <x811v107.sys> <workdir>
// 1. makeDumpSys(x811v107.sys)                                  (the patched update, sha1 printed)
// 2. boot the flash image with WRITE + TYPE held -> IPL mode 0; version request
// 3. install the patched SYS update through the IPL protocol (ms2k_ipl.cpp, as the Windows tool will)
//    -> the emulated flash's system area must equal the patched image byte for byte
// 4. a block with a wrong checksum -> status 02 and the error latch; the whole update again -> OK (the recovery path)
// 5. power on again normally -> the firmware must boot (LCD shows a program)
// 6. power on again in IPL mode (now running the patched IPL) -> dump all 1 MB with command 1 -> must equal the
//    emulated flash
// Nothing here writes the input files; the updated image goes to <workdir>/ipl_e2e_flash.bin.
#include "core/ms2000_runner.h"
#include "core/h8s2350_emulator.h"
#include "core/flash_rom.h"
#include "core/lcd_gui.h"
#include "tools/flashdump/ms2k_ipl.h"
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

using namespace ms2kipl;

static Bytes readFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static bool writeFile(const std::string& p, const uint8_t* d, size_t n)
{
    std::ofstream f(p, std::ios::binary); f.write(reinterpret_cast<const char*>(d), std::streamsize(n)); return bool(f);
}

struct Machine : Transport {
    std::unique_ptr<MS2000::Ms2kRunner> r;
    std::mutex mx; std::deque<uint8_t> out;
    uint64_t ms = 0;
    bool boot(const std::string& rom, bool ipl)
    {
        MS2000::Ms2kConfig cfg;
        cfg.romPath = rom; cfg.cpuCyclesPerTick = 20000; cfg.sampleRate = 48000; cfg.quietBoot = true;
        cfg.useMmcss = false; cfg.audio.enabled = false; cfg.flashStateLoad = false; cfg.flashStateSave = false;
        MS2000::Ms2kGuiHooks hooks; hooks.logFn = [](const char*) {};
        r = std::make_unique<MS2000::Ms2kRunner>(cfg, hooks);
        if (!r->init() || !r->startManual()) return false;
        r->getEmulator().setMidiOutSink([this](uint8_t b) { std::lock_guard<std::mutex> l(mx); out.push_back(b); });
        if (ipl) { r->getEmulator().setPanelSwitch(2, 1, true); r->getEmulator().setPanelSwitch(4, 1, true); }   // TYPE + WRITE
        return true;
    }
    void step(int n) { for (int i = 0; i < n; ++i) { r->tick1ms(); ++ms; } }
    bool send(const Bytes& m) override { r->sendMIDIData(m.data(), m.size()); return true; }
    bool receive(Bytes& sx, int timeoutMs) override
    {
        sx.clear(); bool in = false;
        for (int t = 0; t <= timeoutMs; ++t) {
            for (;;) {
                uint8_t b;
                { std::lock_guard<std::mutex> l(mx); if (out.empty()) break; b = out.front(); out.pop_front(); }
                if (b >= 0xF8) { ++f8; continue; }
                if (b == 0xF0) { sx.assign(1, b); in = true; continue; }
                if (!in) continue;
                sx.push_back(b);
                if (b == 0xF7) return true;
            }
            step(1);
        }
        return false;
    }
    void wait(int w) override { step(w); }
    uint64_t f8 = 0;
};

static int fails = 0;
static void check(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); std::fflush(stdout); if (!ok) ++fails; }

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: ipl_e2e <flash.bin> <x811v107.sys> <workdir>\n"); return 2; }
    const auto wall0 = std::chrono::steady_clock::now();
    const Bytes flash = readFile(argv[1]), v107 = readFile(argv[2]);
    const std::string wd = argv[3];
    check(flash.size() == kFlashSize, "flash image 1 MB");
    check(sha1Hex(v107) == kV107Sha1, "x811v107.sys is KORG's v1.07");
    Bytes patched; std::string err;
    check(makeDumpSys(v107, patched, err), "makeDumpSys " + err);
    std::printf("patched SYS sha1 %s (x811v107 %s)\n", sha1Hex(patched).c_str(), sha1Hex(v107).c_str());
    size_t diff = 0; for (size_t i = 0; i < kSysSize; ++i) diff += patched[i] != v107[i];
    std::printf("bytes changed by the patch: %zu\n", diff);
    const std::string img0 = wd + "/ipl_e2e_in.bin", img1 = wd + "/ipl_e2e_flash.bin";
    writeFile(img0, flash.data(), flash.size());

    // ---- session 1: install ----
    {
        Machine m; check(m.boot(img0, true), "boot 1 (IPL keys held)");
        m.step(1500);
        Version v; Result r = getVersion(m, v);
        check(r.ok, "version request " + r.error);
        std::printf("unit: System %s  PCM %s  User %s  (machine %.1f s)\n", versionText(v.sys).c_str(), versionText(v.pcm).c_str(), versionText(v.usr).c_str(), m.ms / 1000.0);
        // the check's own rule: the dump-patched image may not be sent until validateImage accepts it
        const std::string ve = validateImage(Type::SYS, patched);
        std::printf("validateImage(patched): %s\n", ve.empty() ? "accepted" : ve.c_str());
        auto prog = [&](int d, int n, const std::string& w) { if (d % 8 == 0 || d == n) { std::printf("  %s %d/%d  machine %.1f s\n", w.c_str(), d, n, m.ms / 1000.0); std::fflush(stdout); } return true; };
        // install bypassing nothing: if the sha1 of the patched image is not yet known to validateImage, say so and stop
        if (!ve.empty()) { std::printf("STOP: put the patched sha1 above into kDumpSysSha1 (ms2k_ipl.cpp) and rebuild\n"); return 3; }
        r = install(m, Type::SYS, patched, prog);
        check(r.ok, "install SYS (patched) " + r.error);
        const uint8_t* f = m.r->getEmulator().getFlashROM().data();
        size_t bad = 0; for (size_t i = 0; i < kSysSize; ++i) bad += f[i] != patched[i];
        check(bad == 0, "emulated system area == patched image (" + std::to_string(bad) + " bytes differ)");
        bool rest = true; for (size_t i = kSysSize; i < kFlashSize; ++i) rest = rest && f[i] == flash[i];
        check(rest, "PCM / USER areas untouched");

        // error path: block 0 with a wrong checksum, then the whole update again
        Bytes b = msgBlock(0, patched.data()); b[b.size() - 2] ^= 0x01;
        m.send(b);
        Bytes s; const bool got = m.receive(s, 30000);
        check(got && parseStatus(s) == 2, "wrong checksum -> status 02 (" + (got ? statusText(parseStatus(s)) : std::string("no answer")) + ")");
        { std::lock_guard<std::mutex> l(m.mx); m.out.clear(); }
        m.step(100);
        size_t f8n = 0; { std::lock_guard<std::mutex> l(m.mx); for (uint8_t x : m.out) f8n += x == 0xF8; m.out.clear(); }
        // the RATE is not checked: the spec infers ~1 per 2 ms at phi = 10 MHz; this emulator measures 693 per 100 ms
        // (2026-10-08) - an open timing question of the emulated IPL (TPU2 / SCI1 clocks), not of the protocol
        std::printf("F8 heartbeat while latched: %zu in 100 ms (spec infers one per ~2 ms at phi 10 MHz)\n", f8n);
        check(f8n > 0, "error latch heartbeat (F8) present");
        r = install(m, Type::SYS, patched, prog);
        check(r.ok, "update again after the error " + r.error);
        bad = 0; for (size_t i = 0; i < kSysSize; ++i) bad += f[i] != patched[i];
        check(bad == 0, "system area == patched image after the recovery");
        writeFile(img1, f, kFlashSize);
        m.r->stop();
    }
    // ---- session 2: normal power-on ----
    {
        Machine m; check(m.boot(img1, false), "boot 2 (normal)");
        m.step(6000);
        const LcdGuiSnapshot s = m.r->getLcdGuiSnapshot();
        std::printf("LCD after 6 s: [%s] [%s]\n", s.line0, s.line1);
        check(std::string(s.line0).find(':') != std::string::npos, "the firmware boots with the patched IPL (a program on the LCD)");
        m.r->stop();
    }
    // ---- session 3: dump through the patched IPL ----
    {
        Machine m; check(m.boot(img1, true), "boot 3 (IPL, patched)");
        m.step(1500);
        Version v; Result r = getVersion(m, v);
        check(r.ok, "version request (patched IPL) " + r.error);
        const Bytes ref(m.r->getEmulator().getFlashROM().data(), m.r->getEmulator().getFlashROM().data() + kFlashSize);
        Bytes d;
        r = dump(m, 0, kFlashSize, d, [&](int k, int n, const std::string&) { if (k % 1024 == 0) { std::printf("  dump %d/%d  machine %.1f s\n", k, n, m.ms / 1000.0); std::fflush(stdout); } return true; });
        check(r.ok, "dump 1 MB " + r.error);
        size_t bad = d.size() == ref.size() ? 0 : kFlashSize;
        for (size_t i = 0; i < d.size() && i < ref.size(); ++i) bad += d[i] != ref[i];
        check(bad == 0, "dump == emulated flash (" + std::to_string(bad) + " bytes differ), sha1 " + sha1Hex(d));
        writeFile(wd + "/ipl_e2e_dump.bin", d.data(), d.size());
        m.r->stop();
    }
    std::printf("%s - %d failed, wall %.0f s\n", fails ? "FAILED" : "ALL PASS", fails,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count());
    return fails ? 1 : 0;
}
