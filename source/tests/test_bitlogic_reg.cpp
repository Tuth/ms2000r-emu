// BUG117 - BOR/BIOR, BXOR/BIXOR, BAND/BIAND #xx:3,Rd - every bit number, every register
// field, both inverting forms, both carry-in values, eight operand values.
// Encoding: HM Rev 3.00 Appendix A.2, RENDERED pp.794 (BAND), 796 (BIAND, BIOR),
// 797 (BIXOR), 798 (BOR), 799 (BXOR): `7 4 | i:IMM rd`, `7 5 | i:IMM rd`, `7 6 | i:IMM rd`,
// bit 7 of the 2nd byte = the inverting form. Register field legend RENDERED p.807:
// 0-7 = RnH, 8-15 = RnL. Operation, software manual REJ09B0139 RENDERED p.104 (BXOR):
// C (op) <bit> -> C, the destination operand unchanged, ONLY C affected (H N Z V unchanged).
#include "../core/h8s2350_emulator.h"
#include <cstdio>
#include <cstdint>

using MS2000::H8S2350Emulator;
extern bool g_h8s_quiet_boot;
static const uint32_t CODE = 0x400100;

static void setER(H8S2350Emulator& emu, int n, uint32_t v)
{
    auto& r = emu.getRegisters();
    r.er[n] = v; r.e[n] = uint16_t(v >> 16); r.r[n] = uint16_t(v); r.rh[n] = uint8_t(v >> 8); r.rl[n] = uint8_t(v);
    if (n == 7) r.sp = v & 0xFFFFFF;
}

int main()
{
    g_h8s_quiet_boot = true;
    H8S2350Emulator emu; emu.reset(); emu.setQuietBoot(true);
    const char* names[3][2] = {{"BOR", "BIOR"}, {"BXOR", "BIXOR"}, {"BAND", "BIAND"}};
    const uint8_t vals[] = {0x00, 0xFF, 0x01, 0x80, 0x5A, 0xA5, 0x3C, 0xC3};
    int tests = 0, fails = 0;
    for (int op = 0; op < 3; ++op)
    for (int inv = 0; inv < 2; ++inv)
    for (int bit = 0; bit < 8; ++bit)
    for (int field = 0; field < 16; ++field) {
        const int n = field & 7;
        if (n == 7) continue;                                  // leave the stack pointer alone
        for (uint8_t v : vals) for (int cin = 0; cin < 2; ++cin) for (int hnzv = 0; hnzv < 2; ++hnzv) {
            for (int k = 0; k < 7; ++k) setER(emu, k, 0xA5A5A5A5u ^ (k * 0x11111111u));
            const uint32_t before = (field & 8) ? ((emu.getRegisters().er[n] & ~0xFFu) | v)
                                                : ((emu.getRegisters().er[n] & ~0xFF00u) | (uint32_t(v) << 8));
            setER(emu, n, before);
            const uint8_t b1 = uint8_t((inv ? 0x80 : 0) | (bit << 4) | field);
            emu.writeByte(CODE, uint8_t(0x74 + op)); emu.writeByte(CODE + 1, b1);
            emu.writeByte(CODE + 2, 0x00); emu.writeByte(CODE + 3, 0x00);   // NOP after
            auto& f = emu.getFlags();
            f.carry = cin != 0; f.half_carry = f.negative = f.zero = f.overflow = hnzv != 0;
            emu.getRegisters().pc = CODE;
            emu.step();
            const bool b = ((v >> bit) & 1) != 0, src = inv ? !b : b;
            const bool want = op == 0 ? (cin || src) : op == 1 ? (bool(cin) != src) : (cin && src);
            ++tests;
            const bool ok = f.carry == want && emu.getRegisters().er[n] == before
                         && f.half_carry == bool(hnzv) && f.negative == bool(hnzv) && f.zero == bool(hnzv)
                         && f.overflow == bool(hnzv) && (emu.getRegisters().pc & 0xFFFFFF) == CODE + 2;
            if (!ok) {
                if (++fails <= 12)
                    std::printf("FAIL 7%X %02X %s #%d,R%d%c v=%02X cin=%d: C=%d want %d  ER%d=%08X want %08X  HNZV=%d%d%d%d want %d  PC=%06X\n",
                        4 + op, b1, names[op][inv], bit, n, (field & 8) ? 'L' : 'H', v, cin, f.carry, want,
                        n, emu.getRegisters().er[n], before, f.half_carry, f.negative, f.zero, f.overflow, hnzv,
                        emu.getRegisters().pc & 0xFFFFFF);
            }
        }
    }
    std::printf("[BITLOGIC-REG] %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
