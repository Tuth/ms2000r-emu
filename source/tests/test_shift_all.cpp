// BUG113 - every shift/rotate encoding of groups 10/11/12/13, all sizes, 1 and 2 bits.
// Encodings: HM Rev 3.00 Appendix A.2 (rendered p.805 SHAR/SHLL/SHLR; ROTL/ROTR/ROTXL/ROTXR
// rows of the same table): second byte high nibble 0/4 = .B (1/2 bits), 1/5 = .W, 3/7 = .L
// for SHLL/SHLR/ROTXL/ROTXR; 8/C, 9/D, B/F the same for SHAL/SHAR/ROTL/ROTR.
// Register fields per the legend (rendered p.807). Checked: result, C, N, Z; the other
// register halves untouched. V is not checked here (SHAL sets it; the rest clear it).
#include "../core/h8s2350_emulator.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>

using MS2000::H8S2350Emulator;
extern bool g_h8s_quiet_boot;
static int g_fail = 0, g_tests = 0;
static const uint32_t CODE = 0x400100;

static void setER(H8S2350Emulator& emu, int n, uint32_t v)
{
    auto& r = emu.getRegisters();
    r.er[n] = v; r.e[n] = uint16_t(v >> 16); r.r[n] = uint16_t(v); r.rh[n] = uint8_t(v >> 8); r.rl[n] = uint8_t(v);
    if (n == 7) r.sp = v & 0xFFFFFF;
}

// reference: op 0=SHLL 1=SHAL 2=SHLR 3=SHAR 4=ROTXL 5=ROTL 6=ROTXR 7=ROTR ; bits = 8/16/32
static uint32_t ref(int op, int bits, uint32_t v, int count, bool cin, bool& cout)
{
    const uint32_t mask = bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1);
    const uint32_t msb = 1u << (bits - 1);
    v &= mask; bool c = cin;
    for (int i = 0; i < count; ++i) {
        switch (op) {
        case 0: case 1: c = (v & msb) != 0; v = (v << 1) & mask; break;
        case 2: c = v & 1; v >>= 1; break;
        case 3: { const uint32_t s = v & msb; c = v & 1; v = (v >> 1) | s; break; }
        case 4: { const bool o = (v & msb) != 0; v = ((v << 1) | (c ? 1 : 0)) & mask; c = o; break; }
        case 5: { const bool o = (v & msb) != 0; v = ((v << 1) | (o ? 1 : 0)) & mask; c = o; break; }
        case 6: { const bool o = v & 1; v = (v >> 1) | (c ? msb : 0); c = o; break; }
        case 7: { const bool o = v & 1; v = (v >> 1) | (o ? msb : 0); c = o; break; }
        }
    }
    cout = c; return v;
}

int main()
{
    const char* names[8] = {"SHLL", "SHAL", "SHLR", "SHAR", "ROTXL", "ROTL", "ROTXR", "ROTR"};
    const uint8_t first[8] = {0x10, 0x10, 0x11, 0x11, 0x12, 0x12, 0x13, 0x13};
    const uint32_t vals[] = {0x813E5A01u, 0x7FFF8001u, 0x00000001u, 0xC0000003u, 0x55AA33CCu, 0x013E0000u};
    const char* dbg = std::getenv("SHIFT_DEBUG");   // e.g. SHIFT_DEBUG=3 : that op only, verbose
    g_h8s_quiet_boot = !dbg;
    H8S2350Emulator emu; emu.reset(); emu.setQuietBoot(true);
    for (int op = 0; op < 8; ++op) if (!dbg || op == std::atoi(dbg))
    for (int sz = 0; sz < 3; ++sz)                    // 0=.B 1=.W 2=.L
    for (int two = 0; two < 2; ++two)
    for (int field = 0; field < 16; ++field) {
        if (sz == 2 && field >= 8) continue;          // .L: 0 erd
        for (uint32_t v0 : vals) for (int cin = 0; cin < 2; ++cin) {
            const int n = field & 7;
            for (int k = 0; k < 8; ++k) if (k != 7) setER(emu, k, 0xA5A5A5A5u ^ (k * 0x11111111u));
            setER(emu, n, v0);
            const uint8_t hi = uint8_t((op & 1 ? 8 : 0) | (two ? 4 : 0) | (sz == 0 ? 0 : sz == 1 ? 1 : 3));
            const uint8_t b1 = uint8_t((hi << 4) | field);
            emu.writeByte(CODE, first[op]); emu.writeByte(CODE + 1, b1);
            emu.getFlags().carry = cin != 0;
            emu.getRegisters().pc = CODE;
            emu.step();
            // expected
            uint32_t exp = v0; bool c = false; uint32_t operand, res;
            if (sz == 0) {
                operand = (field & 8) ? (v0 & 0xFF) : ((v0 >> 8) & 0xFF);
                res = ref(op, 8, operand, two ? 2 : 1, cin, c);
                exp = (field & 8) ? ((v0 & ~0xFFu) | res) : ((v0 & ~0xFF00u) | (res << 8));
            } else if (sz == 1) {
                operand = (field & 8) ? (v0 >> 16) : (v0 & 0xFFFF);
                res = ref(op, 16, operand, two ? 2 : 1, cin, c);
                exp = (field & 8) ? ((v0 & 0xFFFFu) | (res << 16)) : ((v0 & 0xFFFF0000u) | res);
            } else {
                res = ref(op, 32, v0, two ? 2 : 1, cin, c);
                exp = res;
            }
            const int bits = sz == 0 ? 8 : sz == 1 ? 16 : 32;
            const bool nExp = (res >> (bits - 1)) & 1, zExp = res == 0;
            ++g_tests;
            auto& f = emu.getFlags();
            const uint32_t got = emu.getRegisters().er[n];
            if (got != exp || f.carry != c || f.negative != nExp || f.zero != zExp || (emu.getRegisters().pc & 0xFFFFFF) != CODE + 2) {
                ++g_fail;
                static bool shown[8][3][2] = {};
                if (!shown[op][sz][two] && (shown[op][sz][two] = true))
                    std::printf("FAIL %02X %02X %s.%c%s field=%d v=%08X cin=%d: ER%d=%08X want %08X  C=%d/%d N=%d/%d Z=%d/%d PC=%06X\n",
                        first[op], b1, names[op], "BWL"[sz], two ? " #2" : "", field, v0, cin, n, got, exp,
                        f.carry, c, f.negative, nExp, f.zero, zExp, emu.getRegisters().pc & 0xFFFFFF);
            }
        }
    }
    std::printf("[SHIFT-ALL] %d tests, %d failures\n", g_tests, g_fail);
    return g_fail ? 1 : 0;
}
