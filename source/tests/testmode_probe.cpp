// AUDIT-1 (2026-10-09): the firmware's own factory test modes (service manual "How to start/operate the test mode")
// run on the EMULATED machine - KORG's code checking our hardware model. Nothing writes flash.bin: the image is a
// copy, flash writes stay in this process (the full test's Flash ROM test also runs Preload).
//   testmode_probe <flash.bin> <seconds> <keys at power-on> [script]
//     keys:   comma list of col.row held at power-on, e.g. 0.6,6.7 = [ON/OFF] + [1]
//     script: ';' list of  <ms>:<col>.<row>:<d|u>  key events after power-on (e.g. 9000:4.2:d;9100:4.2:u = PAGE+)
// Prints the LCD every time it changes (machine time), the LEDs that are lit at each change, MIDI OUT bytes, and at the
// end which flash sectors the firmware changed.
#include "core/ms2000_runner.h"
#include "core/h8s2350_emulator.h"
#include "core/flash_rom.h"
#include "core/lcd_gui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <array>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

struct Ev { uint64_t ms; unsigned c, r; bool d; };

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: testmode_probe <flash.bin> <seconds> <keys c.r,c.r> [script]\n"); return 2; }
    const std::string rom = argv[1];
    const double secs = std::atof(argv[2]);
    std::vector<std::pair<unsigned, unsigned>> keys;
    { std::stringstream ss(argv[3]); std::string k; while (std::getline(ss, k, ',')) { unsigned c, r; if (std::sscanf(k.c_str(), "%u.%u", &c, &r) == 2) keys.push_back({ c, r }); } }
    std::vector<Ev> script;
    std::vector<std::pair<uint64_t, std::array<unsigned, 3>>> pots;   // <ms>:K<mux>.<x>:<0..1023>
    if (argc > 4) { std::stringstream ss(argv[4]); std::string e; while (std::getline(ss, e, ';')) {
        unsigned long long t; unsigned c, r, v; char d;
        if (std::sscanf(e.c_str(), "%llu:K%u.%u:%u", &t, &c, &r, &v) == 4) pots.push_back({ t, { c, r, v } });
        else if (std::sscanf(e.c_str(), "%llu:A%u:%u", &t, &c, &v) == 3) pots.push_back({ t, { 100u + c, 0u, v } });   // rear AN0..AN3
        else if (std::sscanf(e.c_str(), "%llu:%u.%u:%c", &t, &c, &r, &d) == 4) script.push_back({ t, c, r, d == 'd' }); } }
    size_t pi = 0;

    MS2000::Ms2kConfig cfg;
    cfg.romPath = rom; cfg.cpuCyclesPerTick = 20000; cfg.sampleRate = 48000; cfg.quietBoot = true;
    cfg.useMmcss = false; cfg.audio.enabled = false; cfg.flashStateLoad = false; cfg.flashStateSave = false;
    MS2000::Ms2kGuiHooks hooks; hooks.logFn = [](const char*) {};
    MS2000::Ms2kRunner r(cfg, hooks);
    if (!r.init() || !r.startManual()) { std::printf("machine did not start\n"); return 3; }
    std::mutex mx; std::vector<uint8_t> out, loopq;
    static const bool loopNow = std::getenv("MS2K_TM_LOOP") != nullptr;   // the loop cable: TX straight onto RX, at the byte's own time
    r.getEmulator().setMidiOutSink([&](uint8_t b) { { std::lock_guard<std::mutex> l(mx); out.push_back(b); } if (loopNow) r.sendMIDIData(&b, 1); });
    for (auto& k : keys) r.getEmulator().setPanelSwitch(k.first, k.second, true);
    std::vector<uint8_t> before(r.getEmulator().getFlashROM().data(), r.getEmulator().getFlashROM().data() + MS2000::FlashROM::FLASH_SIZE);

    std::string last; size_t si = 0;
    const uint64_t total = uint64_t(secs * 1000.0);
    for (uint64_t ms = 0; ms < total; ++ms) {
        if (ms == 3000) for (auto& k : keys) r.getEmulator().setPanelSwitch(k.first, k.second, false);   // keys let go after 3 s
        for (; pi < pots.size() && pots[pi].first <= ms; ++pi) {
            const auto& p = pots[pi].second;
            if (p[0] >= 100) r.getEmulator().setRearAnalog(p[0] - 100, uint16_t(p[2]));
            else r.getEmulator().setPanelKnob(p[0], p[1], uint16_t(p[2]));
        }
        while (si < script.size() && script[si].ms <= ms) { r.getEmulator().setPanelSwitch(script[si].c, script[si].r, script[si].d); ++si; }
        r.tick1ms();
        static const bool loop = false;   // (the loop cable is in the sink now)
        if (loop) {
            std::vector<uint8_t> o; { std::lock_guard<std::mutex> l(mx); o.swap(loopq); }
            if (!o.empty()) r.sendMIDIData(o.data(), o.size());
        }
        if (ms % 10 == 0) {
            const LcdGuiSnapshot s = r.getLcdGuiSnapshot();
            std::string l0(s.line0), l1(s.line1);
            for (auto* l : { &l0, &l1 }) for (char& ch : *l) if (uint8_t(ch) < 0x20) ch = char('0' + (uint8_t(ch) & 7));   // CGRAM -> digit
            const std::string lcd = (s.displayOn ? "" : "(off) ") + l0 + " | " + l1;
            if (lcd != last) {
                last = lcd;
                std::string leds;
                const auto L = r.getEmulator().panelLeds();
                for (unsigned a = 0; a < 8; ++a) for (unsigned b = 0; b < 12; ++b) if (L.lit[a][b] > 0.5f) { char t[16]; std::snprintf(t, sizeof t, " %u.%u", a, b); leds += t; }
                std::vector<uint8_t> o; { std::lock_guard<std::mutex> l(mx); o.swap(out); }
                std::string mo; for (uint8_t b : o) if (b != 0xFE && b != 0xF8) { char t[8]; std::snprintf(t, sizeof t, " %02X", b); mo += t; if (mo.size() > 60) { mo += " ..."; break; } }
                std::printf("%8.2f  [%s]  LEDs:%s%s%s\n", ms / 1000.0, lcd.c_str(), leds.empty() ? " -" : leds.c_str(), mo.empty() ? "" : "  MIDI:", mo.c_str());
                std::fflush(stdout);
            }
        }
    }
    const uint8_t* f = r.getEmulator().getFlashROM().data();
    std::printf("flash sectors changed by the firmware:");
    for (uint32_t sa = 0; sa < MS2000::FlashROM::NUM_SECTORS; ++sa) {
        uint32_t s = 0, e = 0; MS2000::FlashROM::sectorRange(sa, s, e);
        size_t n = 0; for (uint32_t i = s; i <= e; ++i) n += f[i] != before[i];
        if (n) std::printf(" SA%u(%zu B)", sa, n);
    }
    std::printf("\n");
    r.stop();
    return 0;
}
