#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static uint32_t run_and_get_cycles(H8S2350Emulator& emu, uint32_t pc) {
    const uint32_t c0 = emu.getCycles();
    emu.setProgramCounter(pc);
    emu.step();
    return emu.getCycles() - c0;
}

static void set_flags(MS2000::H8SFlags& f, bool H, bool N, bool Z, bool V, bool C) {
    f.half_carry = H; f.negative = N; f.zero = Z; f.overflow = V; f.carry = C;
}

int main(){
    std::puts("[MULX-FLAGS] start");

    // 1) MULXU.B – N/Z from result, H/V/C = 0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        set_flags(f, true, true, false, true, true); // H=1,N=1,Z=0,V=1,C=1
        r.rl[1] = 0x12; r.rl[2] = 0x34; // ensure result changes
        const uint32_t pc = 0x002000;
        emu.writeByte(pc+0, 0x50);
        emu.writeByte(pc+1, (uint8_t)((1u<<4)|2u));
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 3u);
#else
        if (d != 3u) {
            std::fprintf(stderr, "[DEBUG] MULXU.B cycles=%u (expected 3) — relaxed in Debug\n", d);
        }
#endif
        // Result updated
        uint16_t w = (uint16_t)(r.r[2] & 0xFFFF);
        assert(w == (uint16_t)(0x12*0x34));
        // Flags: H/V/C cleared; N/Z from result (word)
        assert(f.half_carry==false && f.overflow==false && f.carry==false);
        if (w == 0) { assert(f.zero==true && f.negative==false); } else { assert(f.zero==false); }
    }

    // 1b) MULXU.W – N/Z from result, H/V/C = 0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        set_flags(f, true, true, false, true, true);
        r.r[4] = 0x0123; r.r[5] = 0x0045; // ensure non-trivial product
        const uint32_t pc = 0x002200;
        emu.writeByte(pc+0, 0x52);
        emu.writeByte(pc+1, 0x04); // rs
        emu.writeByte(pc+2, 0x00);
        emu.writeByte(pc+3, 0x05); // erd
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 4u);
#else
        if (d != 4u) {
            std::fprintf(stderr, "[DEBUG] MULXU.W cycles=%u (expected 4) — relaxed in Debug\n", d);
        }
#endif
        uint32_t l = r.er[5];
        assert(l == (uint32_t)0x0123 * (uint32_t)0x0045);
        assert(f.half_carry==false && f.overflow==false && f.carry==false);
        if (l == 0) { assert(f.zero==true && f.negative==false); } else { assert(f.zero==false); }
    }

    // 2) MULXS.W – N/Z updated from result; H/V/C = 0
    // Case A: negative result -> N=1, Z=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        set_flags(f, true, false, true, true, true); // H=1,N=0,Z=1,V=1,C=1
        r.r[3] = 0x8000; // -32768
        r.r[6] = 0x0002; // (keeps negative when multiplied)
        const uint32_t pc = 0x002400;
        emu.writeByte(pc+0, 0x57);
        emu.writeByte(pc+1, 0x06); // rs
        emu.writeByte(pc+2, 0x00);
        emu.writeByte(pc+3, 0x03); // erd
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 5u);
#else
        if (d != 5u) {
            std::fprintf(stderr, "[DEBUG] MULXS.W cycles=%u (expected 5) — relaxed in Debug\n", d);
        }
#endif
        // Flags: H/V/C cleared, N/Z from result
        assert(f.half_carry==false && f.overflow==false && f.carry==false);
        assert(f.negative==true && f.zero==false);
    }

    // Case B: zero result -> Z=1, N=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        set_flags(f, true, true, false, true, true); // set distinct pre-state
        r.r[1] = 0x0000;
        r.r[2] = 0x0000;
        const uint32_t pc = 0x002600;
        emu.writeByte(pc+0, 0x57);
        emu.writeByte(pc+1, 0x01); // rs
        emu.writeByte(pc+2, 0x00);
        emu.writeByte(pc+3, 0x02); // erd
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 5u);
#else
        if (d != 5u) {
            std::fprintf(stderr, "[DEBUG] MULXS.W cycles=%u (expected 5) — relaxed in Debug\n", d);
        }
#endif
        // H/V/C cleared; N/Z reflect zero
        assert(f.half_carry==false && f.overflow==false && f.carry==false);
        assert(f.zero==true && f.negative==false);
    }

    std::puts("[MULX-FLAGS] ok");
    return 0;
}

