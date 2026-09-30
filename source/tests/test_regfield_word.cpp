// BUG112 - 16-bit register fields: 0000-0111 = R0-R7, 1000-1111 = E0-E7
// (H8S/2350 HM Rev 3.00, Appendix A register-field legend, RENDERED p.807).
// Encodings from Appendix A.2 (MOV.W 69/6B/6D/6F, CMP.B/W 1C/1D). Flags: CMP sets
// N Z V C from Rd - Rs; MOV sets N Z, clears V, keeps C.
// Code and data live in external RAM at 0x400000 (the firmware's own RAM area).
#include "../core/h8s2350_emulator.h"
#include <cstdio>
#include <cstdint>
#include <initializer_list>

using MS2000::H8S2350Emulator;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const uint32_t CODE = 0x400100, DATA = 0x400800;

static void run(H8S2350Emulator& emu, std::initializer_list<uint8_t> bytes)
{
    uint32_t a = CODE;
    for (uint8_t b : bytes) emu.writeByte(a++, b);
    emu.getRegisters().pc = CODE;
    emu.step();
}
static uint16_t rd16(H8S2350Emulator& emu, uint32_t a) { return uint16_t((emu.readByte(a) << 8) | emu.readByte(a + 1)); }
static void wr16(H8S2350Emulator& emu, uint32_t a, uint16_t v) { emu.writeByte(a, uint8_t(v >> 8)); emu.writeByte(a + 1, uint8_t(v)); }
static void setER(H8S2350Emulator& emu, int n, uint32_t v)
{
    auto& r = emu.getRegisters();
    r.er[n] = v; r.e[n] = uint16_t(v >> 16); r.r[n] = uint16_t(v); r.rh[n] = uint8_t(v >> 8); r.rl[n] = uint8_t(v);
    if (n == 7) r.sp = v & 0xFFFFFF;
}

int main()
{
    std::puts("[REGFIELD-WORD] start");

    { // MOV.W @ER1,E0 = 69 18
        H8S2350Emulator emu; emu.reset();
        setER(emu, 0, 0x11112222); setER(emu, 1, DATA); wr16(emu, DATA, 0xABCD);
        run(emu, {0x69, 0x18});
        CHECK(emu.getRegisters().er[0] == 0xABCD2222u, "69 18 MOV.W @ER1,E0: ER0=%08X", emu.getRegisters().er[0]);
    }
    { // MOV.W E1,@ER3 = 69 B9
        H8S2350Emulator emu; emu.reset();
        setER(emu, 1, 0x5A5A0000); setER(emu, 3, DATA);
        run(emu, {0x69, 0xB9});
        CHECK(rd16(emu, DATA) == 0x5A5A, "69 B9 MOV.W E1,@ER3: mem=%04X", rd16(emu, DATA));
    }
    { // MOV.W @(2,ER1),E2 = 6F 1A 00 02 ; MOV.W E2,@(4,ER1) = 6F 9A 00 04
        H8S2350Emulator emu; emu.reset();
        setER(emu, 1, DATA); setER(emu, 2, 0x00001234); wr16(emu, DATA + 2, 0xBEEF);
        run(emu, {0x6F, 0x1A, 0x00, 0x02});
        CHECK(emu.getRegisters().er[2] == 0xBEEF1234u, "6F 1A MOV.W @(2,ER1),E2: ER2=%08X", emu.getRegisters().er[2]);
        run(emu, {0x6F, 0x9A, 0x00, 0x04});
        CHECK(rd16(emu, DATA + 4) == 0xBEEF, "6F 9A MOV.W E2,@(4,ER1): mem=%04X", rd16(emu, DATA + 4));
    }
    { // MOV.W @ER1+,E3 = 6D 1B ; MOV.W E3,@-ER1 = 6D 9B
        H8S2350Emulator emu; emu.reset();
        setER(emu, 1, DATA); setER(emu, 3, 0x0000FFFF); wr16(emu, DATA, 0x7777);
        run(emu, {0x6D, 0x1B});
        CHECK(emu.getRegisters().er[3] == 0x7777FFFFu, "6D 1B MOV.W @ER1+,E3: ER3=%08X", emu.getRegisters().er[3]);
        CHECK(emu.getRegisters().er[1] == DATA + 2, "6D 1B: ER1=%08X", emu.getRegisters().er[1]);
        setER(emu, 3, 0x3333FFFF);
        run(emu, {0x6D, 0x9B});
        CHECK(rd16(emu, DATA) == 0x3333, "6D 9B MOV.W E3,@-ER1: mem=%04X", rd16(emu, DATA));
    }
    { // MOV.W @0x400800:32,E0 = 6B 28 00 40 08 00 ; MOV.W E0,@0x400804:32 = 6B A8 00 40 08 04
        H8S2350Emulator emu; emu.reset();
        setER(emu, 0, 0x00000004); wr16(emu, DATA, 0x0004);
        run(emu, {0x6B, 0x28, 0x00, 0x40, 0x08, 0x00});
        CHECK(emu.getRegisters().er[0] == 0x00040004u, "6B 28 MOV.W @aa:32,E0: ER0=%08X", emu.getRegisters().er[0]);
        run(emu, {0x6B, 0xA8, 0x00, 0x40, 0x08, 0x04});
        CHECK(rd16(emu, DATA + 4) == 0x0004, "6B A8 MOV.W E0,@aa:32: mem=%04X", rd16(emu, DATA + 4));
    }
    { // CMP.W R0,R4 = 1D 04 : 0x0000 - 0xFFFF -> C=1 Z=0 N=0 V=0, R4 unchanged
        H8S2350Emulator emu; emu.reset();
        setER(emu, 0, 0x0002FFFF); setER(emu, 4, 0x00000000);
        run(emu, {0x1D, 0x04});
        auto& f = emu.getFlags();
        CHECK(f.carry && !f.zero && !f.negative && !f.overflow, "1D 04 CMP.W R0,R4: C=%d Z=%d N=%d V=%d", f.carry, f.zero, f.negative, f.overflow);
        CHECK(emu.getRegisters().er[4] == 0, "1D 04: ER4=%08X changed", emu.getRegisters().er[4]);
    }
    { // CMP.W R3,R4 = 1D 34 : 0x8000 - 0x0001 -> V=1 (neg - pos = pos overflow), N=0, C=0
        H8S2350Emulator emu; emu.reset();
        setER(emu, 3, 0x00000001); setER(emu, 4, 0x12348000);
        run(emu, {0x1D, 0x34});
        auto& f = emu.getFlags();
        CHECK(!f.carry && !f.zero && !f.negative && f.overflow, "1D 34 CMP.W R3,R4: C=%d Z=%d N=%d V=%d", f.carry, f.zero, f.negative, f.overflow);
        CHECK(emu.getRegisters().er[4] == 0x12348000u, "1D 34: ER4=%08X changed", emu.getRegisters().er[4]);
    }
    { // CMP.B R0H,R4H = 1C 04 : 0x00 - 0x01 -> C=1 N=1
        H8S2350Emulator emu; emu.reset();
        setER(emu, 0, 0x00000100); setER(emu, 4, 0x00000055);
        run(emu, {0x1C, 0x04});
        auto& f = emu.getFlags();
        CHECK(f.carry && !f.zero && f.negative && !f.overflow, "1C 04 CMP.B R0H,R4H: C=%d Z=%d N=%d V=%d", f.carry, f.zero, f.negative, f.overflow);
        CHECK(emu.getRegisters().er[4] == 0x00000055u, "1C 04: ER4=%08X changed", emu.getRegisters().er[4]);
    }
    { // CMP.W E1,R2 = 1D 92 : R2 0x0005 - E1 0x0005 -> Z=1 (field 1001 = E1)
        H8S2350Emulator emu; emu.reset();
        setER(emu, 1, 0x00050000); setER(emu, 2, 0x00000005);
        run(emu, {0x1D, 0x92});
        auto& f = emu.getFlags();
        CHECK(f.zero && !f.carry, "1D 92 CMP.W E1,R2: C=%d Z=%d", f.carry, f.zero);
    }

    { // STC.B EXR,R1L = 02 19 ; STC.B CCR,R2H = 02 02 - two bytes (RENDERED p.805)
        H8S2350Emulator emu; emu.reset();
        auto& r = emu.getRegisters();
        r.exr = 0x0085; setER(emu, 1, 0x11111111); setER(emu, 2, 0x22222222);
        run(emu, {0x02, 0x19});
        CHECK(r.er[1] == 0x11111185u, "02 19 STC.B EXR,R1L: ER1=%08X", r.er[1]);
        CHECK((r.pc & 0xFFFFFF) == CODE + 2, "02 19: PC=%06X, want %06X (2 bytes)", r.pc, CODE + 2);
        emu.getFlags().carry = true; emu.getFlags().zero = false;
        run(emu, {0x02, 0x02});
        CHECK((r.er[2] & 0x0100u) != 0 && (r.er[2] & 0x0400u) == 0 && (r.er[2] & 0xFFFF00FFu) == 0x22220022u,
              "02 02 STC.B CCR,R2H: ER2=%08X (want C=1 Z=0 in R2H, rest untouched)", r.er[2]);
    }

    std::printf("[REGFIELD-WORD] %s (%d failures)\n", g_fail ? "FAILED" : "all checks passed", g_fail);
    return g_fail ? 1 : 0;
}
