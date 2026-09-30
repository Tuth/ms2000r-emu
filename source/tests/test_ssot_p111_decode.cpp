#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>
using MS2000::H8S2350Emulator;

static void w2(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1){ e.writeByte(pc, b0); e.writeByte(pc+1,b1); }
static void w3(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1, uint8_t b2){ e.writeByte(pc,b0); e.writeByte(pc+1,b1); e.writeByte(pc+2,b2);} 
static void w4(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3){ e.writeByte(pc,b0); e.writeByte(pc+1,b1); e.writeByte(pc+2,b2); e.writeByte(pc+3,b3);} 

int main(){
    // DAA/DAS decode
    {
        H8S2350Emulator emu; emu.reset(); auto& r=emu.getRegisters(); uint32_t pc=0x1000; r.pc=pc;
        for (uint8_t rd=0; rd<8; ++rd){ w2(emu, pc, 0x0F, rd); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize()); r.pc=pc; w2(emu, pc, 0x1F, rd); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize()); }
    }
    // EXTU/EXTS/NEG decode
    {
        H8S2350Emulator emu; emu.reset(); auto& r=emu.getRegisters(); uint32_t pc=0x2000;
        for(uint8_t rd=0; rd<8; ++rd){ r.pc=pc; w2(emu, pc, 0x17, (uint8_t)((0x5<<4)|rd)); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize());
                                        r.pc=pc; w2(emu, pc, 0x17, (uint8_t)((0xD<<4)|rd)); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize());
                                        r.pc=pc; w2(emu, pc, 0x17, (uint8_t)((0x8<<4)|rd)); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize());
                                        r.pc=pc; w2(emu, pc, 0x17, (uint8_t)((0x9<<4)|rd)); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize()); }
        for(uint8_t erd=0; erd<8; ++erd){ r.pc=pc; w3(emu, pc, 0x17, 0x70, erd); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize());
                                          r.pc=pc; w3(emu, pc, 0x17, 0xF0, erd); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize());
                                          r.pc=pc; w3(emu, pc, 0x17, 0xB0, erd); emu.step(); assert(emu.lastExecPC()==emu.lastExecStart()+emu.lastExecSize()); }
    }
    // MULXU/MULXS decode (manual remap)
    {
        H8S2350Emulator emu; emu.reset(); auto& r=emu.getRegisters(); uint32_t pc=0x3000;
        // MULXU.B: 0x50, (rs<<4)|rd
        for(uint8_t rs=0; rs<16; ++rs){ for(uint8_t rd=0; rd<16; ++rd){ r.pc=pc; w2(emu, pc, 0x50, (uint8_t)((rs<<4)|rd)); emu.step(); assert(emu.getRegisters().pc==pc+2); }}
        // MULXU.W: 0x52, rs, 0x00, erd
        for(uint8_t rs=0; rs<8; ++rs){ for(uint8_t erd=0; erd<8; ++erd){ r.pc=pc; w4(emu, pc, 0x52, rs, 0x00, erd); emu.step(); assert(emu.getRegisters().pc==pc+4); }}
        // MULXS.W: 0x57, rs, 0x00, erd
        for(uint8_t rs=0; rs<8; ++rs){ for(uint8_t erd=0; erd<8; ++erd){ r.pc=pc; w4(emu, pc, 0x57, rs, 0x00, erd); emu.step(); assert(emu.getRegisters().pc==pc+4); }}
    }
    return 0;
}
