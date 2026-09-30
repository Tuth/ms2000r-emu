#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void emit2(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1){ e.writeByte(pc,b0); e.writeByte(pc+1,b1);} 
static void emit4(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3){ e.writeByte(pc,b0); e.writeByte(pc+1,b1); e.writeByte(pc+2,b2); e.writeByte(pc+3,b3);} 

int main(){
    std::puts("[MULX-MATRIX] start");
    // MULXU.B: (B x B) -> W in Rd
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rs=1, rd=0; r.rl[rs]=0x12; r.rl[rd]=0x34; emit2(emu,0x1000,0x50,(uint8_t)((rs<<4)|rd)); r.pc=0x1000; emu.step();
        uint16_t w = (uint16_t)(r.r[rd] & 0xFFFF);
        assert(w == (uint16_t)(0x12*0x34)); assert(f.carry==false && f.half_carry==false && f.overflow==false);
    }
    // MULXU.W: (W x W) -> L in ERd
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rs=4, rd=5; r.r[rs]=0x0102; r.r[rd]=0x0030; emit4(emu,0x3000,0x52,rs,0x00,rd); r.pc=0x3000; emu.step();
        uint32_t lr = r.er[rd]; assert(lr == (uint32_t)0x0102 * (uint32_t)0x0030);
        assert(f.carry==false && f.half_carry==false && f.overflow==false);
    }
    // MULXS.W: (W x W signed) -> L in ERd
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters();
        uint8_t rs=2, rd=3; r.r[rs]=0xFFF0; r.r[rd]=0x0010; emit4(emu,0x4000,0x57,rs,0x00,rd); r.pc=0x4000; emu.step();
        int32_t prod = (int16_t)0xFFF0 * (int16_t)0x0010; uint32_t lr = r.er[rd];
        assert((int32_t)lr == prod);
    }
    std::puts("[MULX-MATRIX] ok");
    return 0;
}
