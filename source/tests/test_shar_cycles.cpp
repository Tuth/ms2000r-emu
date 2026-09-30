// Sprint P1.7: SHAR cycles smoke — all sizes, 1-bit and "#2" forms.
// DoD: each variant consumes exactly H8S_CYC_BASE_ALU.

#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static uint64_t run_one(H8S2350Emulator& emu, uint8_t primary, uint8_t op2) {
    auto& regs = emu.getRegisters();
    regs.pc = 0x001000;

    emu.writeByte(0x001000, primary);
    emu.writeByte(0x001001, op2);

    const auto c0 = emu.getCycles();
    emu.step();
    const auto c1 = emu.getCycles();
    return (c1 - c0);
}

int main() {
    std::puts("=== SHAR cycles smoke ===");

    const uint8_t rs_norm = 0x0;   // rs = 0 -> 1-bit form
    const uint8_t rs_hash2 = 0x2;  // rs bit1 set -> "#2" form per executor
    const uint8_t rd = 0x0;        // R0/ER0 target
    const uint8_t op2_norm  = static_cast<uint8_t>((rs_norm  << 4) | rd);   // 0x00
    const uint8_t op2_hash2 = static_cast<uint8_t>((rs_hash2 << 4) | rd);   // 0x20

    // Guard satisfies (op2 & 0x88) == 0
    assert((op2_norm  & 0x88) == 0);
    assert((op2_hash2 & 0x88) == 0);

    const uint8_t P_B = 0x1B;
    const uint8_t P_W = 0x1C;
    const uint8_t P_L = 0x1D;

    const uint64_t EXPECT = H8S_CYC_BASE_ALU;

    // Byte
    {
        H8S2350Emulator emu; emu.reset();
        auto d = run_one(emu, P_B, op2_norm);
        std::printf("[SHAR.CYC] SHAR.B        cycles=%llu\n", (unsigned long long)d);
        assert(d == EXPECT);
    }
    {
        H8S2350Emulator emu; emu.reset();
        auto d = run_one(emu, P_B, op2_hash2);
        std::printf("[SHAR.CYC] SHAR.B (#2)   cycles=%llu\n", (unsigned long long)d);
        assert(d == EXPECT);
    }

    // Word
    {
        H8S2350Emulator emu; emu.reset();
        auto d = run_one(emu, P_W, op2_norm);
        std::printf("[SHAR.CYC] SHAR.W        cycles=%llu\n", (unsigned long long)d);
        assert(d == EXPECT);
    }
    {
        H8S2350Emulator emu; emu.reset();
        auto d = run_one(emu, P_W, op2_hash2);
        std::printf("[SHAR.CYC] SHAR.W (#2)   cycles=%llu\n", (unsigned long long)d);
        assert(d == EXPECT);
    }

    // Long
    {
        H8S2350Emulator emu; emu.reset();
        auto d = run_one(emu, P_L, op2_norm);
        std::printf("[SHAR.CYC] SHAR.L        cycles=%llu\n", (unsigned long long)d);
        assert(d == EXPECT);
    }
    {
        H8S2350Emulator emu; emu.reset();
        auto d = run_one(emu, P_L, op2_hash2);
        std::printf("[SHAR.CYC] SHAR.L (#2)   cycles=%llu\n", (unsigned long long)d);
        assert(d == EXPECT);
    }

    std::puts("[SHAR.CYC] All SHAR cycle smoke tests passed.");
    return 0;
}

