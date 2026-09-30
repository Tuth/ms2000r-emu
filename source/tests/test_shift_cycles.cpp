#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cstdio>
#include <cassert>

using namespace H8S;
using MS2000::H8S2350Emulator;

static uint64_t run1(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1, bool has_b2=false, uint8_t b2=0){
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
    std::puts("[SHIFT.CYC] start");
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run1(emu, 0x1000, Shift::SHLL, uint8_t((0x0<<4)|0)); // SHLL.B
        assert(d == H8S_CYC_BASE_ALU);
    }
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run1(emu, 0x1000, Shift::SHLR, uint8_t((0x5<<4)|1)); // SHLR.W #2
        assert(d == H8S_CYC_BASE_ALU);
    }
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run1(emu, 0x1000, Shift::ROTL, 0xB0, true, 2); // ROTL.L
        assert(d == H8S_CYC_BASE_ALU);
    }
    {
        H8S2350Emulator emu; emu.reset();
        uint64_t d = run1(emu, 0x1000, Shift::ROTR, 0xF0, true, 3); // ROTR.L #2
        assert(d == H8S_CYC_BASE_ALU);
    }
    std::puts("[SHIFT.CYC] all passed.");
    return 0;
}

