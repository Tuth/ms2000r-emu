// BUG122 - found by the DIFFREF MOV oracle (tools/diffref/movoracle.h), exams from the manual:
//   HM REJ09B0330 Appendix A.2 RENDERED p.801-802: `6D 0:ers rd` MOV.W @ERs+,Rd, `6D 1:erd rs`
//   MOV.W Rs,@-ERd, `78 0:erd 0 6A|6B A rs disp32` MOV.B/W Rs,@(d:32,ERd).
//   The H8S/2350 address bus is 24 bits: the upper 8 bits of ERn take no part in the address.
//   REJ09B0139 MOV: N and Z from the data, V = 0, C unchanged - stores as well as loads.
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
static void run(H8S2350Emulator& emu, std::initializer_list<uint8_t> b, uint8_t ccr)
{
    uint32_t a = CODE; for (uint8_t x : b) emu.writeByte(a++, x);
    emu.writeByte(a, 0); emu.writeByte(a + 1, 0);
    emu.getRegisters().pc = CODE; emu.setCCRFromByte(ccr); emu.step();
}
static void check(bool ok, const char* w) { ++tests; if (!ok) { ++fails; std::printf("FAIL %s\n", w); } }

int main()
{
    g_h8s_quiet_boot = true;
    H8S2350Emulator emu; emu.reset(); emu.setQuietBoot(true);
    auto& r = emu.getRegisters();
    for (uint32_t top : {0x00u, 0x01u, 0x5Au, 0xFFu}) {
        emu.writeByte(0x404552, 0x95); emu.writeByte(0x404553, 0x1F);
        setER(emu, 4, (top << 24) | 0x404552); setER(emu, 2, 0xBB268794);
        run(emu, {0x6D, 0x42}, 0x80);                                   // MOV.W @ER4+,R2
        check(r.er[2] == 0xBB26951F && r.er[4] == ((top << 24) | 0x404554) && (emu.ccrByteLive() & 0x0E) == 0x08, "MOV.W @ERs+,Rd with a top byte");
        emu.writeByte(0x404168, 0x00); emu.writeByte(0x404169, 0x00);
        setER(emu, 2, (top << 24) | 0x40416A); setER(emu, 5, 0x1234FC01);
        run(emu, {0x6D, 0xA5}, 0x80);                                   // MOV.W R5,@-ER2
        check(emu.readByte(0x404168) == 0xFC && emu.readByte(0x404169) == 0x01 && r.er[2] == ((top << 24) | 0x404168), "MOV.W Rs,@-ERd with a top byte");
    }
    setER(emu, 1, 0x00404000); setER(emu, 3, 0x00000080);
    run(emu, {0x78, 0x10, 0x6A, 0xAB, 0x00, 0x00, 0x01, 0x00}, 0x84);   // MOV.B R3L,@(0x100,ER1): Z in, data 0x80
    check(emu.readByte(0x404100) == 0x80 && (emu.ccrByteLive() & 0x0E) == 0x08, "MOV.B Rs,@(d:32,ERd) sets N, clears Z/V");
    setER(emu, 3, 0x00000000);
    run(emu, {0x78, 0x10, 0x6B, 0xA3, 0x00, 0x00, 0x01, 0x00}, 0x8A);   // MOV.W R3,@(0x100,ER1): data 0
    check(emu.readByte(0x404100) == 0 && emu.readByte(0x404101) == 0 && (emu.ccrByteLive() & 0x0E) == 0x04, "MOV.W Rs,@(d:32,ERd) sets Z, clears N/V");
    std::printf("[BUG122-MOV] %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
