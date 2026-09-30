#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void emit2(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1){ e.writeByte(pc,b0); e.writeByte(pc+1,b1);} 
static void emit3(H8S2350Emulator& e, uint32_t pc, uint8_t b0, uint8_t b1, uint8_t b2){ e.writeByte(pc,b0); e.writeByte(pc+1,b1); e.writeByte(pc+2,b2);} 
static uint64_t step(H8S2350Emulator& e, uint32_t pc){ auto c0=e.getCycles(); auto &r=e.getRegisters(); r.pc=pc; e.step(); return e.getCycles()-c0; }

int main(){
    std::puts("[EXT-MATRIX] start");
    // EXTU.W: zero-extend byte -> word; H/V/C unchanged
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags(); f.carry=true; f.half_carry=true; f.overflow=true;
        r.r[0] = (r.r[0] & 0xFF00) | 0x80; emit2(emu,0x1000,0x17,(uint8_t)((0x5<<4)|0x0)); auto d=step(emu,0x1000);
        assert((r.r[0] & 0xFFFF) == 0x0080); assert(f.carry&&f.half_carry&&f.overflow); assert(d==1);
    }
    // EXTS.W: sign-extend byte -> word (manual: 0x17, 0xD|rd)
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); r.r[1]=(r.r[1]&0xFF00)|0x80; emit2(emu,0x2000,0x17,(uint8_t)((0xD<<4)|0x1)); step(emu,0x2000);
        assert((r.r[1] & 0xFFFF) == 0xFF80); // negative
    }
    // EXTU.L: zero-extend word -> long in ERd
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); r.r[2]=0x00FF; emit3(emu,0x3000,0x17,0x70,2); step(emu,0x3000);
        assert(r.er[2] == 0x000000FFu);
    }
    // EXTS.L: sign-extend word -> long in ERd (manual: 0x17, 0xF0, erd)
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); r.r[3]=0x8000; emit3(emu,0x4000,0x17,0xF0,3); step(emu,0x4000);
        assert(r.er[3] == 0xFFFF8000u);
    }
    std::puts("[EXT-MATRIX] ok");
    return 0;
}
