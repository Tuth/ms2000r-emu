// MOV oracle for DIFFREF (DIFFREF_MOV=<n>). Encodings: HM REJ09B0330 Appendix A.2, RENDERED
// p.801-802 (printed 765-766). Operation: REJ09B0139 MOV pages - data -> destination, N and Z
// from the data, V cleared, H and C unchanged; @ERs+ increments after, @-ERd decrements before,
// by 1/2/4; @aa:16 and d:16 are sign-extended. The reference core does not implement 78 (d:32)
// at all, so this oracle is the only judge for those forms.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct MovCase {
    uint8_t code[12]; int len;
    uint32_t er[8];           // input registers
    uint32_t ea; int size;    // 1/2/4
    bool store; int reg;      // register field (byte 0-15 / word 0-15 / long 0-7)
    int postInc = -1, preDec = -1;   // base register index when the form modifies it
    const char* name;
};

// random generator is passed in so runs are reproducible
template <class RND>
inline MovCase makeMov(RND& rnd, uint32_t winLo, uint32_t winHi, uint32_t oncLo, uint32_t oncHi)
{
    MovCase c{}; c.len = 0;
    auto put = [&](uint8_t b) { c.code[c.len++] = b; };
    auto put16 = [&](uint32_t v) { put(uint8_t(v >> 8)); put(uint8_t(v)); };
    auto put32 = [&](uint32_t v) { put(uint8_t(v >> 24)); put(uint8_t(v >> 16)); put(uint8_t(v >> 8)); put(uint8_t(v)); };
    for (int i = 0; i < 8; ++i) c.er[i] = rnd() ^ (rnd() << 16);
    c.er[7] = (winLo + 0x800) & ~3u;
    const int sz = 1 << (rnd() % 3); c.size = sz;          // 1, 2, 4
    c.store = (rnd() & 1) != 0;
    const int mode = rnd() % 7;                              // @ERs, d16, d32, +/-, aa16, aa32
    int base = rnd() % 7, reg;
    if (sz == 4) { reg = rnd() % 7; while (reg == base) reg = rnd() % 7; }
    else { reg = rnd() % 16; while ((reg & 7) == base) reg = rnd() % 16; }
    c.reg = reg;
    uint32_t ea = (winLo + 0x100 + (rnd() % (winHi - winLo - 0x200))) & ~uint32_t(sz == 1 ? 0 : 1);
    const uint8_t rfield = uint8_t(sz == 4 ? reg : reg);
    auto pre = [&]() { if (sz == 4) { put(0x01); put(0x00); } };
    switch (mode) {
        case 0: {   // @ERs / @ERd : 68/69, 01 00 69
            c.name = "@ERn"; c.er[base] = (c.er[base] & 0xFF000000u) | ea; pre();
            put(sz == 1 ? 0x68 : 0x69); put(uint8_t((c.store ? 0x80 : 0) | (base << 4) | rfield)); break; }
        case 1: {   // @(d:16,ERs) : 6E/6F, 01 00 6F
            c.name = "@(d16,ERn)"; int16_t d = int16_t((rnd() % 0x200) - 0x100); if (sz > 1) d &= ~1;
            c.er[base] = (c.er[base] & 0xFF000000u) | ((ea - d) & 0xFFFFFF); pre();
            put(sz == 1 ? 0x6E : 0x6F); put(uint8_t((c.store ? 0x80 : 0) | (base << 4) | rfield)); put16(uint16_t(d)); break; }
        case 2: {   // @(d:32,ERs) : 78 0ers 0 / 6A|6B 2rd|Ars disp32, 01 00 78 ...
            c.name = "@(d32,ERn)"; int32_t d = int32_t(rnd() % 0x20000) - 0x10000; if (sz > 1) d &= ~1;
            c.er[base] = (c.er[base] & 0xFF000000u) | ((ea - d) & 0xFFFFFF); pre();
            put(0x78); put(uint8_t(base << 4)); put(sz == 1 ? 0x6A : 0x6B); put(uint8_t((c.store ? 0xA0 : 0x20) | rfield)); put32(uint32_t(d)); break; }
        case 3: case 4: {   // @ERs+ (load) / @-ERd (store) : 6C/6D, 01 00 6D
            c.name = c.store ? "@-ERn" : "@ERn+"; pre();
            if (c.store) { c.er[base] = (c.er[base] & 0xFF000000u) | ((ea + sz) & 0xFFFFFF); c.preDec = base; }
            else         { c.er[base] = (c.er[base] & 0xFF000000u) | ea; c.postInc = base; }
            put(sz == 1 ? 0x6C : 0x6D); put(uint8_t((c.store ? 0x80 : 0) | (base << 4) | rfield)); break; }
        case 5: {   // @aa:16 -> on-chip RAM (sign-extended F400-FBFF)
            c.name = "@aa16"; ea = (oncLo + 0x40 + (rnd() % (oncHi - oncLo - 0x80))) & ~uint32_t(sz == 1 ? 0 : 1); pre();
            put(sz == 1 ? 0x6A : 0x6B); put(uint8_t((c.store ? 0x80 : 0x00) | rfield)); put16(ea & 0xFFFF); break; }
        default: {  // @aa:32
            c.name = "@aa32"; pre();
            put(sz == 1 ? 0x6A : 0x6B); put(uint8_t((c.store ? 0xA0 : 0x20) | rfield)); put32(ea); break; }
    }
    c.ea = ea;
    return c;
}
