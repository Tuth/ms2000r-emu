#include <algorithm>
#include <chrono>
// DIFFREF - instruction-level differential test: our H8S/2350 core vs the UKNTCH2000
// reference core (local-only; see ref_wrap.c). One instruction per test, random but
// reproducible register/flag/memory state, identical in both cores. Every field that
// differs after the step is a QUESTION, not a verdict: the RENDERED Hitachi manual
// decides who is right. Tests where the reference touched anything outside the shared
// data windows (I/O, flash, vectors) are skipped - the two machines differ there by design.
//
//   diffref [perWord=16] [seed=1] [firstWord=0x0000] [lastWord=0xFFFF] [examples=3]
#include "../../source/core/h8s2350_emulator.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "oracle.h"
#include "movoracle.h"
#include "../../source/core/io_probe.h"

extern "C" {
void ref_init(void);
unsigned char* ref_ram(void);
void ref_set(const unsigned er[8], unsigned pc, unsigned ccr, unsigned exr);
void ref_get(unsigned er[8], unsigned* pc, unsigned* ccr, unsigned* exr);
int ref_step(void);
void ref_reset(void);
long long ref_bench(long long n, unsigned* pcOut);
int ref_nacc(void);
void ref_acc(int i, unsigned* addr, unsigned* size, unsigned* write, unsigned* value);
}
using MS2000::H8S2350Emulator;
extern bool g_h8s_quiet_boot;

static uint32_t WIN = 0x404000, WINSZ = 0x1000;           // external work RAM window (AUDIT-3 sites: all of 0x400000-0x40FFFF)
static const uint32_t ONC = 0xFFF400, ONCSZ = 0x800;      // on-chip RAM (both cores: plain RAM)
static const uint32_t CODE = 0x40E000, CODESZ = 16;

static uint64_t g_rng;
static uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return uint32_t(g_rng >> 16); }

static void setER(H8S2350Emulator& emu, int n, uint32_t v)
{
    auto& r = emu.getRegisters();
    r.er[n] = v; r.e[n] = uint16_t(v >> 16); r.r[n] = uint16_t(v); r.rh[n] = uint8_t(v >> 8); r.rl[n] = uint8_t(v);
    if (n == 7) r.sp = v & 0xFFFFFF;
}
static uint8_t oursCCR(H8S2350Emulator& emu)   // flags struct is the live NZVC/H/I/UI
{
    auto& f = emu.getFlags(); auto& r = emu.getRegisters();
    return uint8_t((f.carry ? 1 : 0) | (f.overflow ? 2 : 0) | (f.zero ? 4 : 0) | (f.negative ? 8 : 0) |
                   (r.ccr & 0x10) | (f.half_carry ? 0x20 : 0) | (r.ccr & 0x40) | (r.ccr & 0x80));
}
static bool inShared(uint32_t a, uint32_t n)
{
    a &= 0xFFFFFF;
    auto in = [&](uint32_t b, uint32_t s) { return a >= b && a + n <= b + s; };
    return in(WIN, WINSZ) || in(ONC, ONCSZ) || in(CODE, CODESZ);
}
static uint32_t randReg(bool ptr)
{
    if (ptr) return (WIN + 0x100 + (rnd() % (WINSZ - 0x200))) & ~1u;
    switch (rnd() % 4) {
        case 0: return rnd() ^ (rnd() << 16);
        case 1: return rnd() & 0xFF;
        case 2: { static const uint32_t e[] = {0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0x00010000};
                  return e[rnd() % 12]; }
        default: return rnd() & 0xFFFF;
    }
}
static uint8_t randExt()
{
    switch (rnd() % 8) { case 0: case 1: return 0x00; case 2: return 0x40; case 3: return 0xFF; case 4: return uint8_t(0x40 + (rnd() & 0x0F) - 8); default: return uint8_t(rnd()); }
}

struct Key { std::string what; uint16_t word; bool operator<(const Key& o) const { return word != o.word ? word < o.word : what < o.what; } };
struct Rec { uint64_t n = 0; std::vector<std::string> ex; };

int main(int argc, char** argv)
{
    const int perWord = argc > 1 ? atoi(argv[1]) : 16;
    g_rng = argc > 2 ? strtoull(argv[2], 0, 0) : 1; if (!g_rng) g_rng = 1;
    const uint32_t w0 = argc > 3 ? strtoul(argv[3], 0, 0) : 0, w1 = argc > 4 ? strtoul(argv[4], 0, 0) : 0xFFFF;
    const size_t nex = argc > 5 ? strtoul(argv[5], 0, 0) : 3;
    const bool wordMode = getenv("DIFFREF_BYWORD") != nullptr;   // key by full first word instead of first byte + 2nd-byte high nibble

    g_h8s_quiet_boot = true;
    H8S2350Emulator emu; emu.reset(); emu.setQuietBoot(true);
    // PERF-REF (2026-10-01, measurement only): DIFFREF_BENCH=<steps> - both cores from the reset vector of
    // flash.bin (current folder), n steps each, wall time per step. Ours: the whole step() (CPU + peripherals +
    // bus model); the reference: H8SStepCPU only, tracing off, its peripherals stubbed (ref_wrap.c).
    if (const char* bn = getenv("DIFFREF_BENCH")) {
        const long long n = atoll(bn);
        std::vector<uint8_t> img(0x100000, 0xFF);
        if (FILE* f = fopen("flash.bin", "rb")) { fread(img.data(), 1, img.size(), f); fclose(f); } else { printf("no flash.bin\n"); return 2; }
        ref_init(); memcpy(ref_ram(), img.data(), img.size()); ref_reset();
        unsigned rpc = 0;
        auto t0 = std::chrono::steady_clock::now();
        const long long rdone = ref_bench(n, &rpc);
        const double rs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        H8S2350Emulator me; me.setQuietBoot(true);
        if (!me.loadFirmwareFromFile("flash.bin")) { printf("our load failed\n"); return 2; }
        me.reset();
        t0 = std::chrono::steady_clock::now();
        for (long long i = 0; i < n; ++i) me.step();
        const double os = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("[BENCH] reference: %lld steps in %.3f s = %.1f ns/step (%.1f M/s), PC now %06X\n", rdone, rs, rs * 1e9 / double(rdone > 0 ? rdone : 1), double(rdone) / rs / 1e6, rpc);
        printf("[BENCH] ours     : %lld steps in %.3f s = %.1f ns/step (%.1f M/s), PC now %06X, MCU time %.3f s\n", n, os, os * 1e9 / double(n), double(n) / os / 1e6,
               me.getRegisters().pc, double(me.getCycles()) / double(me.getClockFrequency()));
        if (getenv("DIFFREF_HIST")) {   // PERF-MCU: primary-opcode histogram of the bench run
            const auto& h = me.opcodeHits(); std::vector<std::pair<uint64_t,int>> v;
            uint64_t tot = 0; for (int i = 0; i < 256 && i < (int)h.size(); ++i) { tot += h[i]; if (h[i]) v.push_back({h[i], i}); }
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < 32; ++i) printf("[HIST] %02X %6.2f %%\n", v[i].second, 100.0 * double(v[i].first) / double(tot ? tot : 1));
        }
        return 0;
    }
    // DIFFREF_ONE="01 00 78 00 6B A3 00 40 14 5A" [DIFFREF_REGS="er0,er1,...,er7" hex] - run ONE
    // instruction on our core only and print every register and every byte of 0x400000-0x40FFFF
    // and FFF400-FFFBFF that changed. For forms the reference does not implement (78, 7B, ...).
    if (const char* one = getenv("DIFFREF_ONE")) {
        uint32_t a = CODE; for (const char* q = one; *q; ) { while (*q == ' ') ++q; if (!*q) break; emu.writeByte(a++, uint8_t(strtoul(q, (char**)&q, 16))); }
        emu.writeByte(a, 0); emu.writeByte(a + 1, 0);
        uint32_t er[8] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x00404800};
        if (const char* rg = getenv("DIFFREF_REGS")) { const char* q = rg; for (int i = 0; i < 8 && *q; ++i) { er[i] = strtoul(q, (char**)&q, 16); if (*q == ',') ++q; } }
        for (int i = 0; i < 8; ++i) setER(emu, i, er[i]);
        // DIFFREF_MEM="404000:01 02 03;FFF500:AA" - preset memory before the step
        if (const char* mm = getenv("DIFFREF_MEM")) { const char* q = mm;
            while (*q) { uint32_t ad = strtoul(q, (char**)&q, 16); if (*q == ':') ++q;
                while (*q && *q != ';') { while (*q == ' ') ++q; if (!*q || *q == ';') break; emu.writeByte(ad++, uint8_t(strtoul(q, (char**)&q, 16))); }
                if (*q == ';') ++q; } }
        static uint8_t before[0x10000 + 0x800];
        for (uint32_t i = 0; i < 0x10000; ++i) before[i] = emu.readByte(0x400000 + i);
        for (uint32_t i = 0; i < 0x800; ++i) before[0x10000 + i] = emu.readByte(0xFFF400 + i);
        emu.getRegisters().pc = CODE; emu.setCCRFromByte(0x00);
        emu.step();
        auto& r = emu.getRegisters();
        printf("[ONE] PC=%06X CCR=%02X halted=%d\n", r.pc & 0xFFFFFF, emu.ccrByteLive(), int(emu.isHalted()));
        for (int i = 0; i < 8; ++i) printf("[ONE] ER%d %08X -> %08X%s\n", i, er[i], r.er[i], er[i] != r.er[i] ? "  *" : "");
        for (uint32_t i = 0; i < 0x10000; ++i) { uint8_t v = emu.readByte(0x400000 + i); if (v != before[i] && (0x400000 + i < CODE || 0x400000 + i >= CODE + CODESZ)) printf("[ONE] MEM %06X %02X -> %02X\n", 0x400000 + i, before[i], v); }
        for (uint32_t i = 0; i < 0x800; ++i) { uint8_t v = emu.readByte(0xFFF400 + i); if (v != before[0x10000 + i]) printf("[ONE] MEM %06X %02X -> %02X\n", 0xFFF400 + i, before[0x10000 + i], v); }
        return 0;
    }
    // DIFFREF_MULDIV=<n>: MULXU/MULXS/DIVXU/DIVXS .B/.W against REJ09B0139 RENDERED p.101-108
    // (DIVXS/DIVXU), p.151-154 (MULXS/MULXU): Rd is a 16-bit field (R0-R7, E0-E7) for .B, ERd for
    // .W; Rs is 8-bit (.B) or 16-bit (.W). Quotient low half, remainder high half, remainder sign =
    // dividend sign. Flags: MULXU none; MULXS N,Z of the product; DIVXU N = divisor negative,
    // Z = divisor zero; DIVXS N = quotient negative, Z = divisor zero. Division by zero and
    // quotient overflow are skipped (undefined).
    if (const char* md = getenv("DIFFREF_MULDIV")) {
        const uint64_t n = strtoull(md, nullptr, 0); uint64_t bad = 0, run = 0;
        std::map<std::string, uint64_t> cnt, bc;
        auto rb = [](const uint32_t* e, int f) -> uint32_t { return f < 8 ? (e[f & 7] >> 8) & 0xFF : e[f & 7] & 0xFF; };
        auto rw = [](const uint32_t* e, int f) -> uint32_t { return f < 8 ? e[f & 7] & 0xFFFF : e[f & 7] >> 16; };
        auto ww = [](uint32_t* e, int f, uint32_t v) { if (f < 8) e[f & 7] = (e[f & 7] & 0xFFFF0000u) | (v & 0xFFFF); else e[f & 7] = (e[f & 7] & 0xFFFFu) | (v << 16); };
        for (uint64_t t = 0; t < n; ++t) {
            uint32_t er[8]; for (int i = 0; i < 7; ++i) er[i] = rnd() ^ (rnd() << 16); er[7] = 0x00404800;
            if (rnd() & 1) for (int i = 0; i < 7; ++i) er[i] &= (rnd() & 1) ? 0x00FF00FFu : 0xFFFFFFFFu;
            const int op = rnd() % 8;               // 0 MULXU.B 1 MULXU.W 2 DIVXU.B 3 DIVXU.W 4 MULXS.B 5 MULXS.W 6 DIVXS.B 7 DIVXS.W
            const bool word = op & 1, div = (op & 2) != 0, sgn = op >= 4;
            int rs = rnd() % 16, rd = word ? int(rnd() % 7) : int(rnd() % 16);
            if (word ? ((rs & 7) == rd) : ((rs & 7) == (rd & 7))) continue;   // no register overlap
            if (!word && (rd & 7) == 7) continue; if ((rs & 7) == 7) continue;
            uint8_t code[6]; int len = 0;
            if (sgn) { code[len++] = 0x01; code[len++] = div ? 0xD0 : 0xC0; }
            code[len++] = uint8_t(0x50 | (div ? 1 : 0) | (word ? 2 : 0));
            code[len++] = uint8_t((rs << 4) | rd);
            uint32_t x[8]; for (int i = 0; i < 8; ++i) x[i] = er[i];
            const uint8_t ccrIn = uint8_t((rnd() & 0x7F) | 0x80); uint8_t xc = ccrIn;
            if (!div) {
                if (!word) { uint32_t a = rb(x, rd + 8 * 0) , b = rb(x, rs); a = rw(x, rd) & 0xFF;
                    uint32_t p = sgn ? uint32_t(int32_t(int8_t(a)) * int32_t(int8_t(b))) & 0xFFFF : (a * b) & 0xFFFF;
                    ww(x, rd, p); if (sgn) xc = uint8_t((xc & ~0x0C) | ((p & 0x8000) ? 8 : 0) | (p == 0 ? 4 : 0)); }
                else { uint32_t a = x[rd] & 0xFFFF, b = rw(x, rs);
                    uint32_t p = sgn ? uint32_t(int32_t(int16_t(a)) * int32_t(int16_t(b))) : a * b;
                    x[rd] = p; if (sgn) xc = uint8_t((xc & ~0x0C) | ((p & 0x80000000u) ? 8 : 0) | (p == 0 ? 4 : 0)); }
            } else {
                if (!word) { uint32_t a = rw(x, rd), b = rb(x, rs); if (!b) continue;
                    uint32_t q, r;
                    if (sgn) { int32_t sa = int16_t(a), sb = int8_t(b), sq = sa / sb, sr = sa % sb; if (sq < -128 || sq > 127) continue; q = uint32_t(sq) & 0xFF; r = uint32_t(sr) & 0xFF;
                               xc = uint8_t((xc & ~0x0C) | (sq < 0 ? 8 : 0)); }
                    else { q = a / b; r = a % b; if (q > 0xFF) continue; xc = uint8_t((xc & ~0x0C) | ((b & 0x80) ? 8 : 0)); }
                    ww(x, rd, (r << 8) | q); }
                else { uint32_t a = x[rd], b = rw(x, rs); if (!b) continue;
                    uint32_t q, r;
                    if (sgn) { int64_t sa = int32_t(a), sb = int16_t(b), sq = sa / sb, sr = sa % sb; if (sq < -32768 || sq > 32767) continue; q = uint32_t(sq) & 0xFFFF; r = uint32_t(sr) & 0xFFFF;
                               xc = uint8_t((xc & ~0x0C) | (sq < 0 ? 8 : 0)); }
                    else { q = a / b; r = a % b; if (q > 0xFFFF) continue; xc = uint8_t((xc & ~0x0C) | ((b & 0x8000) ? 8 : 0)); }
                    x[rd] = (r << 16) | q; }
            }
            ++run;
            for (int i = 0; i < len; ++i) emu.writeByte(CODE + i, code[i]); emu.writeByte(CODE + len, 0); emu.writeByte(CODE + len + 1, 0);
            for (int i = 0; i < 8; ++i) setER(emu, i, er[i]);
            emu.setCCRFromByte(ccrIn); emu.getRegisters().pc = CODE; emu.step();
            auto& r = emu.getRegisters();
            static const char* nm[8] = {"MULXU.B", "MULXU.W", "DIVXU.B", "DIVXU.W", "MULXS.B", "MULXS.W", "DIVXS.B", "DIVXS.W"};
            cnt[nm[op]]++;
            std::string why;
            for (int i = 0; i < 8; ++i) if (r.er[i] != x[i]) why += " ER" + std::to_string(i);
            if (emu.ccrByteLive() != xc) why += " CCR";
            if ((r.pc & 0xFFFFFF) != CODE + uint32_t(len)) why += " PC";
            if (emu.isHalted()) { why += " HALT"; emu.reset(); emu.resume(); }
            if (!why.empty()) { ++bad; if (bc[nm[op]]++ < 4) {
                printf("[MD-DIFF] %s:%s code:", nm[op], why.c_str()); for (int i = 0; i < len; ++i) printf(" %02X", code[i]); printf("\n");
                for (int i = 0; i < 8; ++i) if (r.er[i] != x[i] || er[i] != x[i]) printf("     ER%d in=%08X want=%08X ours=%08X\n", i, er[i], x[i], r.er[i]);
                printf("     CCR in=%02X want=%02X ours=%02X\n", ccrIn, xc, emu.ccrByteLive()); } }
        }
        printf("[DIFFREF-MULDIV] %llu run, %llu differ\n", (unsigned long long)run, (unsigned long long)bad);
        for (auto& c : cnt) printf("[MD-FORM] %-8s %8llu tests %8llu bad\n", c.first.c_str(), (unsigned long long)c.second, (unsigned long long)bc[c.first]);
        return bad ? 1 : 0;
    }
    if (const char* mv = getenv("DIFFREF_MOV")) {
        const uint64_t n = strtoull(mv, nullptr, 0);
        uint64_t bad = 0; std::map<std::string, uint64_t> byForm, badForm;
        for (uint64_t t = 0; t < n; ++t) {
            MovCase c = makeMov(rnd, WIN, WIN + WINSZ, ONC, ONC + ONCSZ);
            for (int i = 0; i < c.len; ++i) emu.writeByte(CODE + i, c.code[i]);
            emu.writeByte(CODE + c.len, 0); emu.writeByte(CODE + c.len + 1, 0);
            for (uint32_t i = 0; i < 8; ++i) emu.writeByte(c.ea - 4 + i, uint8_t(rnd()));   // fresh data around EA
            for (int i = 0; i < 8; ++i) setER(emu, i, c.er[i]);
            const uint8_t ccrIn = uint8_t((rnd() & 0x7F) | 0x80);  // I=1: no interrupt may be taken inside the test
            emu.setCCRFromByte(ccrIn); emu.getRegisters().pc = CODE;
            uint8_t memBefore[8]; for (int i = 0; i < 8; ++i) memBefore[i] = emu.readByte(c.ea - 4 + i);
            // expected
            uint32_t xer[8]; for (int i = 0; i < 8; ++i) xer[i] = c.er[i];
            uint8_t xmem[8]; for (int i = 0; i < 8; ++i) xmem[i] = memBefore[i];
            if (c.preDec >= 0) xer[c.preDec] -= c.size;
            uint32_t data;
            if (c.store) {
                if (c.size == 1) data = (c.reg < 8) ? (c.er[c.reg & 7] >> 8) & 0xFF : c.er[c.reg & 7] & 0xFF;
                else if (c.size == 2) data = (c.reg < 8) ? c.er[c.reg & 7] & 0xFFFF : c.er[c.reg & 7] >> 16;
                else data = c.er[c.reg];
                for (int i = 0; i < c.size; ++i) xmem[4 + i] = uint8_t(data >> (8 * (c.size - 1 - i)));
            } else {
                data = 0; for (int i = 0; i < c.size; ++i) data = (data << 8) | memBefore[4 + i];
                uint32_t& d = xer[c.reg & 7];
                if (c.size == 1) d = (c.reg < 8) ? ((d & 0xFFFF00FFu) | (data << 8)) : ((d & 0xFFFFFF00u) | data);
                else if (c.size == 2) d = (c.reg < 8) ? ((d & 0xFFFF0000u) | data) : ((d & 0x0000FFFFu) | (data << 16));
                else d = data;
            }
            if (c.postInc >= 0) xer[c.postInc] += c.size;
            const uint32_t sign = c.size == 1 ? 0x80u : c.size == 2 ? 0x8000u : 0x80000000u;
            const uint8_t xccr = uint8_t((ccrIn & 0xF1) | ((data & sign) ? 8 : 0) | (data == 0 ? 4 : 0));
            emu.step();
            auto& r = emu.getRegisters();
            std::string why;
            for (int i = 0; i < 8; ++i) if (r.er[i] != xer[i]) why += " ER" + std::to_string(i);
            for (int i = 0; i < 8; ++i) if (emu.readByte(c.ea - 4 + i) != xmem[i]) { why += " MEM"; break; }
            if (emu.ccrByteLive() != xccr) why += " CCR";
            if ((r.pc & 0xFFFFFF) != CODE + uint32_t(c.len)) why += " PC";
            if (emu.isHalted()) { why += " HALT"; emu.reset(); emu.resume(); }
            const std::string form = std::string(c.store ? "ST" : "LD") + ".bwl"[0] + std::string(1, ".BW?L"[c.size]) + " " + c.name;
            byForm[form]++;
            if (!why.empty()) { ++bad; if (badForm[form]++ < 3) {
                printf("[MOV-DIFF] %s:%s  code:", form.c_str(), why.c_str()); for (int i = 0; i < c.len; ++i) printf(" %02X", c.code[i]);
                printf("  ea=%06X\n", c.ea);
                for (int i = 0; i < 8; ++i) if (r.er[i] != xer[i]) printf("     ER%d in=%08X want=%08X ours=%08X\n", i, c.er[i], xer[i], r.er[i]);
                printf("     CCR in=%02X want=%02X ours=%02X  PC want=%06X ours=%06X\n", ccrIn, xccr, emu.ccrByteLive(), CODE + c.len, r.pc & 0xFFFFFF);
                printf("     mem want:"); for (int i = 0; i < 8; ++i) printf(" %02X", xmem[i]); printf("  ours:"); for (int i = 0; i < 8; ++i) printf(" %02X", emu.readByte(c.ea - 4 + i)); printf("\n"); } }
        }
        printf("[DIFFREF-MOV] %llu tests, %llu differ\n", (unsigned long long)n, (unsigned long long)bad);
        for (auto& f : byForm) printf("[MOV-FORM] %-24s %8llu tests %8llu bad\n", f.first.c_str(), (unsigned long long)f.second, (unsigned long long)badForm[f.first]);
        return bad ? 1 : 0;
    }
    // AUDIT-3: DIFFREF_SITES=<file of "pc size bytes" lines from MS2K_OPCOV> - replay exactly the encodings the
    // firmware executes (deduplicated by bytes), perWord random states each, instead of sweeping first words.
    std::vector<std::vector<uint8_t>> sites; std::vector<uint32_t> sitePc;
    if (const char* sf = getenv("DIFFREF_SITES")) {
        std::map<std::vector<uint8_t>, uint32_t> uniq;
        if (FILE* f = fopen(sf, "r")) {
            char line[256];
            while (fgets(line, sizeof line, f)) {
                unsigned pc = 0, sz = 0; int n = 0;
                if (sscanf(line, "%x %u%n", &pc, &sz, &n) < 2 || sz == 0 || sz > 10) continue;
                std::vector<uint8_t> b; const char* p = line + n; unsigned v; int m;
                while (b.size() < sz && sscanf(p, "%x%n", &v, &m) == 1) { b.push_back(uint8_t(v)); p += m; }
                if (b.size() == sz) uniq.emplace(b, pc);
            }
            fclose(f);
        }
        for (auto& u : uniq) { sites.push_back(u.first); sitePc.push_back(u.second); }
        WIN = 0x400000; WINSZ = 0x10000;   // the firmware's own work RAM, so its @aa operands stay comparable
        printf("[SITES] %zu distinct encodings from %s\n", sites.size(), sf);
        if (sites.empty()) return 2;
    }
    const bool siteMode = !sites.empty();
    std::vector<uint32_t> siteCmp(sites.size()), siteUnk(sites.size()), siteOut(sites.size());
    // the firmware runs interrupt control MODE 2 (SYSCR = H'21, BUG114): RTE then pops EXR too
    auto fwMode = [&] { if (siteMode) emu.writeByte(0xFFFF39, 0x21); };
    fwMode();
    ref_init();
    unsigned char* R = ref_ram();

    auto fillWin = [&]() {
        for (uint32_t i = 0; i < WINSZ; ++i) { uint8_t v = uint8_t(rnd()); R[WIN + i] = v; emu.writeByte(WIN + i, v); }
        for (uint32_t i = 0; i < ONCSZ; ++i) { uint8_t v = uint8_t(rnd()); R[ONC + i] = v; emu.writeByte(ONC + i, v); }
    };
    fillWin();

    std::map<Key, Rec> diffs;
    uint64_t tests = 0, refUnknown = 0, refAbort = 0, outside = 0, compared = 0, bad = 0;
    std::map<uint16_t, uint64_t> unknownFirstWords;
    static uint64_t cov[256][4];   // per first byte: tests, refUnknown, refOutside, compared

    const uint32_t nOuter = siteMode ? uint32_t(sites.size()) : (w1 - w0 + 1);
    for (uint32_t oi = 0; oi < nOuter; ++oi) {
        const uint32_t w = siteMode ? (uint32_t(sites[oi][0]) << 8 | sites[oi][1]) : w0 + oi;
        for (int k = 0; k < perWord; ++k) {
            ++tests; cov[w >> 8][0]++;
            if ((tests & 1023) == 0) fillWin();
            uint8_t code[CODESZ];
            code[0] = uint8_t(w >> 8); code[1] = uint8_t(w);
            for (uint32_t i = 2; i < CODESZ; ++i) code[i] = randExt();
            // two-word prefixes (01xx, 6A/7C-7F...): give the 2nd word a fair chance to be a real opcode
            if (k & 1) { static const uint8_t p[] = {0x69, 0x6B, 0x6D, 0x6F, 0x78, 0x63, 0x73, 0x67, 0x77, 0x70, 0x72, 0x74, 0x75, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x66, 0x6A, 0x7D, 0x7F, 0x7C, 0x7E, 0x6E, 0x6C, 0x68, 0x10, 0x11, 0x12, 0x13, 0x17, 0x1A, 0x1B, 0x1F, 0x0A, 0x0B, 0x0F, 0x53, 0x51, 0x50, 0x52};
                         code[2] = p[rnd() % sizeof p]; }
            if (siteMode) for (size_t i = 0; i < sites[oi].size(); ++i) code[i] = sites[oi][i];
            for (uint32_t i = 0; i < CODESZ; ++i) { R[CODE + i] = code[i]; emu.writeByte(CODE + i, code[i]); }

            unsigned er[8]; for (int i = 0; i < 7; ++i) er[i] = randReg((rnd() & 1) != 0);
            er[7] = (WIN + 0x800 + (rnd() % 0x400)) & ~1u;
            if (siteMode) er[7] = (ONC + 0x400 + (rnd() % 0x300)) & ~1u;   // the firmware keeps its stack on chip
            const unsigned ccr = rnd() & 0xFF, exr = (rnd() & 0x87) | (siteMode ? 0x07u : 0u);   // mode 2: mask 7 = no interrupt inside the test
            ref_set(er, CODE, ccr, exr);
            const int rs = ref_step();
            if (rs == 0) { if (siteMode) siteUnk[oi]++; ++refUnknown; cov[w >> 8][1]++; unknownFirstWords[uint16_t(w)]++; continue; }
            if (rs < 0) { ++refAbort; continue; }
            bool ok = true;
            for (int i = 0, n = ref_nacc(); i < n && ok; ++i) { unsigned a, s, wr, v; ref_acc(i, &a, &s, &wr, &v); if (!inShared(a, s)) ok = false; }
            if (ref_nacc() > 64) ok = false;
            unsigned rer[8], rpc, rccr, rexr; ref_get(rer, &rpc, &rccr, &rexr);
            if (!ok) {   // undo the reference's writes inside the window is unnecessary: it touched something else; resync
                for (uint32_t i = 0; i < WINSZ; ++i) R[WIN + i] = emu.readByte(WIN + i);
                for (uint32_t i = 0; i < ONCSZ; ++i) R[ONC + i] = emu.readByte(ONC + i);
                ++outside; if (siteMode) siteOut[oi]++; cov[w >> 8][2]++; continue;
            }

            for (int i = 0; i < 8; ++i) setER(emu, i, er[i]);
            auto& r = emu.getRegisters();
            r.pc = CODE; r.exr = uint16_t(exr);
            emu.setCCRFromByte(uint8_t(ccr));
            auto& f = emu.getFlags(); f.half_carry = (ccr & 0x20) != 0; f.interrupt_mask = (ccr & 0x80) != 0; f.user_bit = (ccr & 0x40) != 0;
            const uint8_t occrPre = uint8_t(ccr);
            if (siteMode) { IoProbe::enable(); IoProbe::clear(); emu.clearLastInsnBus(); }
            emu.step();
            if (siteMode && IoProbe::count()) {   // OUR core touched I/O: not comparable, and its peripherals may now
                ++outside; siteOut[oi]++;           // hold a pending interrupt - start the next test from a clean machine
                emu.reset(); emu.resume(); fwMode(); fillWin(); continue;
            }
            if (siteMode) {   // OUR core read/wrote off-chip memory outside the shared windows (the reference's own
                bool off = emu.lastInsnBusCount() >= 16;   // log misses some of its reads, so ours decides)
                for (uint32_t i = 0; i < emu.lastInsnBusCount() && !off; ++i) if (!inShared(emu.lastInsnBusAddr(i), 1)) off = true;
                if (off) {
                    ++outside; siteOut[oi]++;
                    for (uint32_t i = 0; i < WINSZ; ++i) R[WIN + i] = emu.readByte(WIN + i);
                    for (uint32_t i = 0; i < ONCSZ; ++i) R[ONC + i] = emu.readByte(ONC + i);
                    continue;
                }
            }
            ++compared; if (siteMode) siteCmp[oi]++; cov[w >> 8][3]++;

            std::vector<std::string> what;
            char buf[160];
            OracleOut orc; uint32_t erIn32[8]; for (int i = 0; i < 8; ++i) erIn32[i] = er[i];
            const bool useOracle = aluOracle(code, erIn32, uint8_t(ccr), CODE, orc);
            if (useOracle) {   // the judge is the manual-derived oracle, not the reference
                for (int i = 0; i < 8; ++i) rer[i] = orc.er[i];
                rpc = orc.pc; rccr = (orc.ccr & 0x2F) | (occrPre & 0xD0); rexr = exr;
            }
            for (int i = 0; i < 8; ++i) if (r.er[i] != rer[i]) { snprintf(buf, sizeof buf, "ER%d", i); what.push_back(buf); }
            for (int i = 0; i < 8; ++i) if (r.e[i] != uint16_t(r.er[i] >> 16) || r.r[i] != uint16_t(r.er[i]) || r.rh[i] != uint8_t(r.er[i] >> 8) || r.rl[i] != uint8_t(r.er[i]))
                { snprintf(buf, sizeof buf, "MIRROR%d", i); what.push_back(buf); }
            if ((r.pc & 0xFFFFFF) != (rpc & 0xFFFFFF)) what.push_back("PC");
            const uint8_t occr = oursCCR(emu);
            if ((occr & 0x0F) != (rccr & 0x0F)) what.push_back("NZVC");
            // The reference never computes H (21 "TODO: H" in its source, zero CCR_H writes):
            // H is compared only where the reference merely MOVES a byte into CCR (02-07 group).
            if ((occr & 0x20) != (rccr & 0x20) && (useOracle || ((w >> 8) >= 0x02 && (w >> 8) <= 0x07))) what.push_back("H");
            if ((occr & 0xD0) != (rccr & 0xD0)) what.push_back("I/UI/U");
            if ((r.ccr & 0xF0) != (occr & 0xF0) && (ccr & 0x20) != 0) {}
            if (uint8_t(r.exr) != uint8_t(rexr)) what.push_back("EXR");
            int memd = 0; uint32_t firstMem = 0;
            for (uint32_t i = 0; i < WINSZ; ++i) { uint8_t o = emu.readByte(WIN + i); if (o != R[WIN + i]) { if (!memd++) firstMem = WIN + i; emu.writeByte(WIN + i, R[WIN + i]); } }
            for (uint32_t i = 0; i < ONCSZ; ++i) { uint8_t o = emu.readByte(ONC + i); if (o != R[ONC + i]) { if (!memd++) firstMem = ONC + i; emu.writeByte(ONC + i, R[ONC + i]); } }
            if (memd) what.push_back("MEM");
            const bool halted = emu.isHalted();
            if (halted) { what.clear(); what.push_back("OURS-ILLEGAL"); }
            if (what.empty()) continue;
            ++bad;
            if (useOracle) what.insert(what.begin(), "ORACLE");
            std::string ws; for (auto& s : what) { if (!ws.empty()) ws += ","; ws += s; }
            if (siteMode) { ws += "  site"; for (uint8_t b : sites[oi]) { snprintf(buf, sizeof buf, " %02X", b); ws += buf; }
                            snprintf(buf, sizeof buf, " @%06X", sitePc[oi]); ws += buf; }
            const uint16_t kw = (wordMode || siteMode) ? uint16_t(w) : uint16_t((w & 0xFF00) | (((w >> 8) == 0x01 || (w >> 8) == 0x6A || ((w >> 8) >= 0x7C && (w >> 8) <= 0x7F) || (w >> 8) == 0x17 || (w >> 8) == 0x0A || (w >> 8) == 0x1A || (w >> 8) == 0x0B || (w >> 8) == 0x1B || (w >> 8) == 0x79 || (w >> 8) == 0x7A || (w >> 8) == 0x10 || (w >> 8) == 0x11 || (w >> 8) == 0x12 || (w >> 8) == 0x13) ? (w & 0xF0) : 0));
            Rec& rec = diffs[{ws, kw}];
            if (rec.n++ < nex) {
                std::string e;
                snprintf(buf, sizeof buf, "   code:"); e += buf;
                for (uint32_t i = 0; i < 10; ++i) { snprintf(buf, sizeof buf, " %02X", code[i]); e += buf; }
                snprintf(buf, sizeof buf, "  CCRin=%02X EXRin=%02X\n", ccr, exr); e += buf;
                for (int i = 0; i < 8; ++i) if (r.er[i] != rer[i] || er[i] != rer[i] || getenv("DIFFREF_ALLREGS")) { snprintf(buf, sizeof buf, "     ER%d in=%08X  ref=%08X  ours=%08X\n", i, er[i], rer[i], r.er[i]); e += buf; }
                snprintf(buf, sizeof buf, "     PC ref=%06X ours=%06X   CCR ref=%02X ours=%02X (shadow %02X)  EXR ref=%02X ours=%02X\n",
                         rpc & 0xFFFFFF, r.pc & 0xFFFFFF, rccr, occr, unsigned(r.ccr & 0xFF), rexr & 0xFF, unsigned(r.exr & 0xFF)); e += buf;
                if (memd) { snprintf(buf, sizeof buf, "     MEM %d byte(s) differ, first @%06X\n", memd, firstMem); e += buf; }
                for (int i = 0, n = ref_nacc(); i < n; ++i) { unsigned a, s, wr, v; ref_acc(i, &a, &s, &wr, &v);
                    if (a >= CODE && a < CODE + CODESZ && !wr) continue;
                    snprintf(buf, sizeof buf, "     ref %s%u @%06X = %0*X\n", wr ? "W" : "R", s * 8, a, int(s * 2), v); e += buf; }
                rec.ex.push_back(e);
            }
            if (halted) { emu.reset(); emu.resume(); fwMode(); fillWin(); }
        }
    }
    printf("[DIFFREF] tests=%llu compared=%llu differ=%llu refUnknown=%llu refAbort=%llu refOutside=%llu\n",
           (unsigned long long)tests, (unsigned long long)compared, (unsigned long long)bad,
           (unsigned long long)refUnknown, (unsigned long long)refAbort, (unsigned long long)outside);
    if (getenv("DIFFREF_COV")) for (int b = 0; b < 256; ++b) if (cov[b][0] && cov[b][3] * 2 < cov[b][0])
        printf("[COV] %02X tests=%llu refUnknown=%llu refOutside=%llu compared=%llu\n", b, (unsigned long long)cov[b][0],
               (unsigned long long)cov[b][1], (unsigned long long)cov[b][2], (unsigned long long)cov[b][3]);
    if (siteMode) {   // AUDIT-3: the encodings no comparison reached - these are the ones to read on the rendered pages
        size_t nu = 0;
        for (size_t i = 0; i < sites.size(); ++i) if (!siteCmp[i]) {
            ++nu; printf("[UNCOMPARED] %s @%06X:", siteUnk[i] ? "ref-unknown" : "outside    ", sitePc[i]);
            for (uint8_t b : sites[i]) printf(" %02X", b);
            printf("\n"); }
        printf("[SITES] %zu encodings, %zu never compared\n", sites.size(), nu);
    }
    for (auto& d : diffs) {
        printf("[DIFF] %04X%s %-28s %llu\n", d.first.word, wordMode ? "" : "*", d.first.what.c_str(), (unsigned long long)d.second.n);
        for (auto& e : d.second.ex) printf("%s", e.c_str());
    }
    return 0;
}
