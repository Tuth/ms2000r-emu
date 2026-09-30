#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void write2(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1){
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
}

int main(){
    std::puts("=== SSoT ADDX decode smoke ===");
    // Reg-Reg: 0x0E, (rs<<4)|rd with rs,rd in 0..15
    {
        H8S2350Emulator emu; emu.reset(); auto& regs = emu.getRegisters();
        const uint32_t pc0 = 0x001000;
        for (uint8_t rs=0; rs<16; ++rs){
            for (uint8_t rd=0; rd<16; ++rd){
                regs.pc = pc0;
                write2(emu, pc0, 0x0E, static_cast<uint8_t>((rs<<4)|rd));
                emu.step();
                assert(emu.getRegisters().pc == pc0 + 2);
            }
        }
    }
    // Imm8->Rd: (0x90|rd), imm8 with rd in 0..15
    {
        H8S2350Emulator emu; emu.reset(); auto& regs = emu.getRegisters();
        const uint32_t pc0 = 0x002000;
        for (uint8_t rd=0; rd<16; ++rd){
            regs.pc = pc0;
            write2(emu, pc0, static_cast<uint8_t>(0x90 | rd), 0x55);
            emu.step();
            assert(emu.getRegisters().pc == pc0 + 2);
        }
    }
    std::puts("[SMOKE-OK] ADDX decode across RR and IMM8 forms");
    return 0;
}

