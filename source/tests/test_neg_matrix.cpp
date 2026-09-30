#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void emit2(MS2000::H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1){ e.writeByte(pc,b0); e.writeByte(pc+1,b1);} 
static void emit3(MS2000::H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1, uint8_t b2){ e.writeByte(pc,b0); e.writeByte(pc+1,b1); e.writeByte(pc+2,b2);} 

int main(){
    std::puts("[NEG-MATRIX] start");
    // NEG.B edges
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags(); r.r[0]=(r.r[0]&0xFF00)|0x00; emit2(emu,0x1000,0x17,(uint8_t)((0x8<<4)|0x0)); r.pc=0x1000; emu.step();
        assert((r.r[0]&0xFF)==0x00 && f.carry==false && f.half_carry==false && f.overflow==false && f.zero==true && f.negative==false);
        r.r[1]=(r.r[1]&0xFF00)|0x80; emit2(emu,0x1002,0x17,(uint8_t)((0x8<<4)|0x1)); r.pc=0x1002; emu.step();
        assert((r.r[1]&0xFF)==0x80 && f.overflow==true && f.carry==true);
    }
    // NEG.W min negative
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags(); r.r[2]=0x8000; emit2(emu,0x2000,0x17,(uint8_t)((0x9<<4)|0x2)); r.pc=0x2000; emu.step();
        assert((r.r[2]&0xFFFF)==0x8000 && f.overflow==true && f.carry==true && f.negative==true);
    }
    // NEG.L random
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); r.er[3]=0x00000001u; emit3(emu,0x3000,0x17,0xB0,3); r.pc=0x3000; emu.step();
        assert(r.er[3]==0xFFFFFFFFu);
    }
    std::puts("[NEG-MATRIX] ok");
    return 0;
}

