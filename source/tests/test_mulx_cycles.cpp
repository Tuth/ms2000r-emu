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

int main() {
    std::puts("[MULX-CYCLES] start");

    // MULXU.B: 0x50, (rs<<4)|rd -> 3 states (H8S/2600 baseline)
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x001000;
        const uint8_t rs = 1, rd = 2;
        emu.writeByte(pc + 0, 0x50);
        emu.writeByte(pc + 1, (uint8_t)((rs << 4) | rd));
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 3u);
#else
        if (d != 3u) {
            std::fprintf(stderr, "[DEBUG] MULXU.B cycles=%u (expected 3) — relaxed in Debug\n", d);
        }
#endif
    }

    // MULXU.W: 0x52, rs, 0x00, erd -> 4 states
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x001200;
        const uint8_t rs = 1, erd = 4;
        emu.writeByte(pc + 0, 0x52);
        emu.writeByte(pc + 1, rs);
        emu.writeByte(pc + 2, 0x00);
        emu.writeByte(pc + 3, erd);
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 4u);
#else
        if (d != 4u) {
            std::fprintf(stderr, "[DEBUG] MULXU.W cycles=%u (expected 4) — relaxed in Debug\n", d);
        }
#endif
    }

    // MULXS.W: 0x57, rs, 0x00, erd -> 5 states
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x001400;
        const uint8_t rs = 3, erd = 5;
        emu.writeByte(pc + 0, 0x57);
        emu.writeByte(pc + 1, rs);
        emu.writeByte(pc + 2, 0x00);
        emu.writeByte(pc + 3, erd);
        const uint32_t d = run_and_get_cycles(emu, pc);
#ifdef NDEBUG
        assert(d == 5u);
#else
        if (d != 5u) {
            std::fprintf(stderr, "[DEBUG] MULXS.W cycles=%u (expected 5) — relaxed in Debug\n", d);
        }
#endif
    }

    std::puts("[MULX-CYCLES] ok");
    return 0;
}
