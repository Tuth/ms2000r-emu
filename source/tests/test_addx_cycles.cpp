#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static uint64_t run1(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1){
    auto& regs = emu.getRegisters();
    regs.pc = pc;
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
    auto c0 = emu.getCycles();
    emu.step();
    return emu.getCycles() - c0;
}

int main(){
    std::puts("[ADDX.CYC] start");
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run1(emu, 0x3000, 0x0E, 0x01); // ADDX R0L+R0H? just encoding size/cycles path
        assert(d == H8S_CYC_BASE_ALU);
    }
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run1(emu, 0x3000, static_cast<uint8_t>(0x90 | 0x0F), 0x7F); // ADDX #imm,R7H
        assert(d == H8S_CYC_BASE_ALU);
    }
    std::puts("[ADDX.CYC] all passed.");
    return 0;
}

