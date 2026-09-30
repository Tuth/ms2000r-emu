// BUG120 - the bit group on @aa:16 / @aa:32: every row, every bit, inverting forms, carry in.
// Encoding HM Rev 3.00 Appendix A.2, RENDERED pp.758-762/799:
//   6A 10/18 aa16 | op | x0      (6 bytes)      6A 30/38 aa32 | op | x0   (8 bytes)
//   op 60-63 = BSET/BNOT/BCLR/BTST Rn (x = rn), 67 = BST/BIST, 70-77 = BSET BNOT BCLR BTST
//   BOR BXOR BAND BLD (#xx:3), bit 7 of the last byte = inverting form (BIST BIOR BIXOR BIAND BILD).
// Flags (A.1): BTST -> Z only; BOR/BXOR/BAND/BLD -> C only; the rest touch no flag.
#include "../core/h8s2350_emulator.h"
#include <cstdio>
#include <cstdint>

using MS2000::H8S2350Emulator;
extern bool g_h8s_quiet_boot;
static const uint32_t CODE = 0x400100, DATA = 0xFFF500;

int main()
{
    g_h8s_quiet_boot = true;
    H8S2350Emulator emu; emu.reset(); emu.setQuietBoot(true);
    const uint8_t ops[] = {0x60, 0x61, 0x62, 0x63, 0x67, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77};
    const uint8_t vals[] = {0x00, 0xFF, 0x5A, 0xA5, 0x01, 0x80};
    int tests = 0, fails = 0;
    for (int aa32 = 0; aa32 < 2; ++aa32)
    for (uint8_t op : ops)
    for (int inv = 0; inv < 2; ++inv) {
        const bool invertible = op == 0x67 || (op >= 0x74 && op <= 0x77);
        if (inv && !invertible) continue;
        for (int bit = 0; bit < 8; ++bit)
        for (uint8_t v : vals) for (int cin = 0; cin < 2; ++cin) for (int zin = 0; zin < 2; ++zin) {
            auto& r = emu.getRegisters();
            const bool regForm = (op & 0xF0) == 0x60 && op != 0x67;
            uint8_t last = uint8_t((inv ? 0x80 : 0) | (bit << 4));
            if (regForm) { r.er[3] = 0xA5A5A500u | uint32_t(bit | 0xF8); r.r[3] = uint16_t(r.er[3]); r.rh[3] = uint8_t(r.er[3] >> 8);
                           r.rl[3] = uint8_t(r.er[3]); last = 0xB0; }   // rn = R3L, only bits 2-0 count
            const bool write = op != 0x63 && op != 0x73 && !(op >= 0x74 && op <= 0x77);
            uint32_t a = CODE;
            emu.writeByte(a++, 0x6A);
            emu.writeByte(a++, uint8_t((aa32 ? 0x30 : 0x10) | (write ? 0x08 : 0x00)));
            if (aa32) { emu.writeByte(a++, 0x00); emu.writeByte(a++, 0xFF); }
            emu.writeByte(a++, 0xF5); emu.writeByte(a++, 0x00);
            emu.writeByte(a++, op); emu.writeByte(a++, last);
            emu.writeByte(a, 0x00); emu.writeByte(a + 1, 0x00);
            emu.writeByte(DATA, v);
            auto& f = emu.getFlags();
            f.carry = cin != 0; f.zero = zin != 0; f.negative = f.overflow = f.half_carry = false;
            r.pc = CODE;
            emu.step();
            const bool b = ((v >> bit) & 1) != 0, src = inv ? !b : b;
            uint8_t wantMem = v; bool wantC = cin, wantZ = zin;
            switch (op) {
                case 0x60: case 0x70: wantMem = uint8_t(v | (1 << bit)); break;
                case 0x61: case 0x71: wantMem = uint8_t(v ^ (1 << bit)); break;
                case 0x62: case 0x72: wantMem = uint8_t(v & ~(1 << bit)); break;
                case 0x63: case 0x73: wantZ = !b; break;
                case 0x67: { const bool c = inv ? !cin : bool(cin); wantMem = uint8_t((v & ~(1 << bit)) | (c << bit)); break; }
                case 0x74: wantC = cin || src; break;
                case 0x75: wantC = bool(cin) != src; break;
                case 0x76: wantC = cin && src; break;
                case 0x77: wantC = src; break;
            }
            ++tests;
            const uint8_t mem = emu.readByte(DATA);
            const uint32_t wantPc = CODE + (aa32 ? 8 : 6);
            if (mem != wantMem || f.carry != wantC || f.zero != wantZ || (r.pc & 0xFFFFFF) != wantPc) {
                if (++fails <= 12)
                    std::printf("FAIL 6A %s op=%02X inv=%d bit=%d v=%02X cin=%d zin=%d: mem %02X/%02X C %d/%d Z %d/%d PC %06X/%06X\n",
                                aa32 ? "aa32" : "aa16", op, inv, bit, v, cin, zin, mem, wantMem, f.carry, wantC, f.zero, wantZ,
                                r.pc & 0xFFFFFF, wantPc);
            }
        }
    }
    std::printf("[BITMEM-ABS] %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
