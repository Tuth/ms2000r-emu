// BUG121 - the defects DIFFREF (tools/diffref, local-only differential test) exposed, as exams
// written from the RENDERED manual pages, not from either core:
//   REJ09B0139 (H8S/2600 software manual) RENDERED p.99 DEC.W, p.100 DEC.L, p.117 INC.W,
//   p.118 INC.L: V = overflow only - Notes list H'7FFF+1, H'7FFF+2, H'7FFE+2 / H'8000-1,
//   H'8000-2, H'8001-2 (and the 32-bit equivalents); H and C unchanged.
//   p.48-50 ADD.B/W/L, p.91-93 CMP.B/W/L, p.233/235/236 SUB.B/W/L: H = carry/borrow at bit 3/11/27.
//   HM REJ09B0330 Appendix A.2 RENDERED p.800: LDC #xx:8,CCR = `0 7 | IMM`, two bytes.
//   REJ09B0139 p.48: ADD.B #xx:8,Rd = `8 rd | IMM` for EVERY rd, R6H included.
//   ORC/ANDC/XORC (HM p.794/803/807): CCR op #imm -> CCR, all eight bits, from the LIVE CCR.
#include "../core/h8s2350_emulator.h"
#include <cstdio>
#include <cstdint>
#include <initializer_list>

using MS2000::H8S2350Emulator;
extern bool g_h8s_quiet_boot;
static const uint32_t CODE = 0x400100;
static int tests = 0, fails = 0;

static void setER(H8S2350Emulator& emu, int n, uint32_t v)
{
    auto& r = emu.getRegisters();
    r.er[n] = v; r.e[n] = uint16_t(v >> 16); r.r[n] = uint16_t(v); r.rh[n] = uint8_t(v >> 8); r.rl[n] = uint8_t(v);
    if (n == 7) r.sp = v & 0xFFFFFF;
}
static void run(H8S2350Emulator& emu, std::initializer_list<uint8_t> bytes, uint8_t ccr)
{
    uint32_t a = CODE; for (uint8_t b : bytes) emu.writeByte(a++, b);
    emu.writeByte(a, 0x00); emu.writeByte(a + 1, 0x00);
    emu.getRegisters().pc = CODE; emu.setCCRFromByte(ccr);
    emu.step();
}
static void check(bool ok, const char* what)
{
    ++tests; if (!ok) { ++fails; std::printf("FAIL %s\n", what); }
}

int main()
{
    g_h8s_quiet_boot = true;
    H8S2350Emulator emu; emu.reset(); emu.setQuietBoot(true);
    auto& f = emu.getFlags(); auto& r = emu.getRegisters();

    // INC.W / DEC.W, every value that the Notes name and its neighbours, both amounts, V in 0 and 1
    for (int vin = 0; vin < 2; ++vin) for (uint32_t x = 0; x <= 0xFFFF; x += (x < 0x10 || (x > 0x7FF0 && x < 0x8010) || x > 0xFFF0) ? 1 : 0x101) {
        for (int amt = 1; amt <= 2; ++amt) {
            setER(emu, 2, 0xABCD0000u | x);
            run(emu, {0x0B, uint8_t(amt == 1 ? 0x52 : 0xD2)}, vin ? 0x02 : 0x00);
            const uint16_t res = uint16_t(x + amt);
            const bool v = (x == 0x7FFF) || (amt == 2 && x == 0x7FFE);
            check(r.er[2] == (0xABCD0000u | res) && f.overflow == v && f.zero == (res == 0) && f.negative == ((res & 0x8000) != 0) && !f.carry, "INC.W");
            setER(emu, 2, 0xABCD0000u | x);
            run(emu, {0x1B, uint8_t(amt == 1 ? 0x52 : 0xD2)}, vin ? 0x03 : 0x00);   // C=1 in: must stay
            const uint16_t d = uint16_t(x - amt);
            const bool dv = (x == 0x8000) || (amt == 2 && x == 0x8001);
            check(r.er[2] == (0xABCD0000u | d) && f.overflow == dv && f.zero == (d == 0) && f.negative == ((d & 0x8000) != 0) && (f.carry == bool(vin)), "DEC.W");
        }
    }
    // INC.L / DEC.L around the 32-bit boundaries
    for (uint32_t x : {0x00000000u, 0x00000001u, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0x80000002u, 0xFFFFFFFFu, 0xFFFFFFFEu})
        for (int amt = 1; amt <= 2; ++amt) {
            setER(emu, 3, x); run(emu, {0x0B, uint8_t(amt == 1 ? 0x73 : 0xF3)}, 0x02);
            check(r.er[3] == x + amt && f.overflow == (x == 0x7FFFFFFFu || (amt == 2 && x == 0x7FFFFFFEu)), "INC.L");
            setER(emu, 3, x); run(emu, {0x1B, uint8_t(amt == 1 ? 0x73 : 0xF3)}, 0x02);
            check(r.er[3] == x - amt && f.overflow == (x == 0x80000000u || (amt == 2 && x == 0x80000001u)), "DEC.L");
        }
    // H: SUB.B/W/L, CMP.B #imm, ADD.L, ADD/CMP/SUB.W #imm, CMP/SUB.L #imm - both senses, H in opposite
    struct { uint32_t d, s; } hv[] = {{0x10, 0x01}, {0x1F, 0x01}, {0x1000, 0x0001}, {0x1FFF, 0x0001}};
    setER(emu, 0, 0x00000010); setER(emu, 1, 0x00000001);
    run(emu, {0x18, 0x98}, 0x00); check(f.half_carry, "SUB.B H borrow at bit 3");       // R0L(0x10) - R1L(0x01)
    setER(emu, 0, 0x0000001F); run(emu, {0x18, 0x98}, 0x20); check(!f.half_carry, "SUB.B H clear");
    setER(emu, 0, 0x00001000); run(emu, {0x19, 0x10}, 0x00); check(f.half_carry, "SUB.W H borrow at bit 11");
    setER(emu, 0, 0x10000000); run(emu, {0x1A, 0x90}, 0x00); check(f.half_carry, "SUB.L H borrow at bit 27");
    setER(emu, 0, 0x00000010); run(emu, {0xA8, 0x01}, 0x00); check(f.half_carry, "CMP.B #imm H");
    setER(emu, 0, 0x0FFFFFFF); run(emu, {0x0A, 0x90}, 0x00); check(f.half_carry && r.er[0] == 0x10000000, "ADD.L H carry at bit 27");
    setER(emu, 0, 0x00000FFF); run(emu, {0x79, 0x10, 0x00, 0x01}, 0x00); check(f.half_carry, "ADD.W #imm H carry at bit 11");
    setER(emu, 0, 0x00001000); run(emu, {0x79, 0x20, 0x00, 0x01}, 0x00); check(f.half_carry, "CMP.W #imm H");
    setER(emu, 0, 0x00001000); run(emu, {0x79, 0x30, 0x00, 0x01}, 0x00); check(f.half_carry, "SUB.W #imm H");
    setER(emu, 0, 0x10000000); run(emu, {0x7A, 0x20, 0x00, 0x00, 0x00, 0x01}, 0x00); check(f.half_carry, "CMP.L #imm H borrow at bit 27");
    setER(emu, 0, 0x08000000); run(emu, {0x7A, 0x20, 0x00, 0x00, 0x00, 0x01}, 0x20); check(!f.half_carry, "CMP.L #imm H: no borrow at bit 27");
    (void)hv;
    // LDC #xx:8,CCR is two bytes
    run(emu, {0x07, 0x00}, 0xFF); check((r.pc & 0xFFFFFF) == CODE + 2 && emu.ccrByteLive() == 0x00, "LDC #xx:8,CCR length");
    // ADD.B #xx:8,R6H
    setER(emu, 6, 0x12344556); run(emu, {0x86, 0x10}, 0x00); check(r.er[6] == 0x12345556 && !emu.isHalted(), "ADD.B #xx:8,R6H");
    // ORC/ANDC work on the live CCR: set Z by arithmetic, then ANDC #0x7F must keep Z
    setER(emu, 0, 0x00000001); run(emu, {0x1A, 0x08}, 0x80);   // DEC.B R0L -> 0, Z=1 (flags struct)
    emu.writeByte(CODE, 0x06); emu.writeByte(CODE + 1, 0x7F); r.pc = CODE; emu.step();   // no CCR reload between
    check(f.zero && (emu.ccrByteLive() & 0x80) == 0, "ANDC #0x7F keeps live Z");
    // LDC sets H, STC reads it back
    run(emu, {0x07, 0x20}, 0x00); check(f.half_carry && (emu.ccrByteLive() & 0x20), "LDC #0x20 sets H");

    std::printf("[BUG121-FLAGS] %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
