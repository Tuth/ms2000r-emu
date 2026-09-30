#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cassert>
#include <cstdio>

using namespace H8S;
using MS2000::H8S2350Emulator;

static uint64_t run(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1, bool has_b2=false, uint8_t b2=0){
    auto& regs = emu.getRegisters();
    regs.pc = pc;
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
    if (has_b2) emu.writeByte(pc+2, b2);
    auto c0 = emu.getCycles();
    emu.step();
    return emu.getCycles() - c0;
}

int main(){
    std::puts("[P1.9.CYC] start");
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run(emu, 0x2000, 0x12, uint8_t((0x0u<<4)|0)); // ROTXL.B 1-bit
        assert(d == H8S_CYC_BASE_ALU);
    }
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run(emu, 0x2000, 0x13, uint8_t((0x5u<<4)|1)); // ROTXR.W #2
        assert(d == H8S_CYC_BASE_ALU);
    }
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run(emu, 0x2000, 0x10, 0xB0, true, 2); // SHAL.L 1-bit
        assert(d == H8S_CYC_BASE_ALU);
    }
    std::puts("[P1.9.CYC] all passed.");
    return 0;
}

