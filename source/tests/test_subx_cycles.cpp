// Cycle smoke for SUBX (P1.6)
#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cassert>
#include <cstdio>

using namespace MS2000;

static void run_rr(uint8_t rs, uint8_t rd, bool preC)
{
    H8S2350Emulator emu; emu.reset();
    const uint32_t pc0 = 0x001000;
    emu.getRegisters().pc = pc0;

    // SUBX.B Rs,Rd  =>  0x1E, (Rs<<4)|Rd
    emu.writeByte(pc0 + 0, 0x1E);
    emu.writeByte(pc0 + 1, static_cast<uint8_t>(((rs & 7) << 4) | (rd & 7)));

    // Seed and flags
    emu.getRegisters().r[rd & 7] = (emu.getRegisters().r[rd & 7] & 0xFFFFFF00u) | 0x12u;
    emu.getRegisters().r[rs & 7] = (emu.getRegisters().r[rs & 7] & 0xFFFFFF00u) | 0x03u;
    emu.getFlags().carry = preC;

    emu.resetCycles();
    const auto c0 = emu.getCycles();
    emu.step();
    const auto delta = emu.getCycles() - c0;
    assert(delta == H8S_CYC_BASE_ALU && "SUBX RR cycle mismatch");
}

static void run_imm(uint8_t rd, uint8_t imm8, bool preC)
{
    H8S2350Emulator emu; emu.reset();
    const uint32_t pc0 = 0x002000;
    emu.getRegisters().pc = pc0;

    // SUBX.B #imm8,Rd  => (0xB0|Rd), imm8
    emu.writeByte(pc0 + 0, static_cast<uint8_t>(0xB0 | (rd & 7)));
    emu.writeByte(pc0 + 1, imm8);

    // Seed and flags
    emu.getRegisters().r[rd & 7] = (emu.getRegisters().r[rd & 7] & 0xFFFFFF00u) | 0x12u;
    emu.getFlags().carry = preC;

    emu.resetCycles();
    const auto c0 = emu.getCycles();
    emu.step();
    const auto delta = emu.getCycles() - c0;
    assert(delta == H8S_CYC_BASE_ALU && "SUBX IMM cycle mismatch");
}

int main()
{
    // reg-reg
    run_rr(/*Rs*/1, /*Rd*/0, /*C*/false);
    run_rr(/*Rs*/2, /*Rd*/3, /*C*/true);

    // imm8
    run_imm(/*Rd*/4, /*imm*/0x07, /*C*/false);
    run_imm(/*Rd*/5, /*imm*/0xF0, /*C*/true);

    std::puts("[SUBX-CYCLES] All cycle smoke cases passed");
    return 0;
}
