#include "../core/h8s2350_emulator.h"
#include <cstdio>
#include <cassert>

using MS2000::H8S2350Emulator;

static void one_step(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1, bool has_b2=false, uint8_t b2=0){
    auto& regs = emu.getRegisters();
    regs.pc = pc;
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
    if (has_b2) emu.writeByte(pc+2, b2);
    emu.step();
}

int main(){
    std::puts("[SHIFT-MATRIX] start...");

    // SHLL.B 1-bit: 0x80 -> 0x00, C=1, Z=1, N=0, V=0
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=3; regs.r[rd]=0x0080; one_step(emu,0x1000,0x10, uint8_t((0x0<<4)|rd));
        uint8_t res=uint8_t(regs.r[rd]&0xFF); assert(res==0x00); assert(f.carry); assert(f.zero); assert(!f.negative); assert(!f.overflow);
    }
    // SHLL.B #2: 0x40 -> 0x00, C=1
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=2; regs.r[rd]=0x0040; one_step(emu,0x1000,0x10, uint8_t((0x4<<4)|rd));
        uint8_t res=uint8_t(regs.r[rd]&0xFF); assert(res==0x00); assert(f.carry); assert(f.zero); assert(!f.overflow);
    }
    // SHLR.B 1-bit: 0x03 -> 0x01, C=1, N=0
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=1; regs.r[rd]=0x0003; one_step(emu,0x1000,0x11, uint8_t((0x0<<4)|rd));
        uint8_t res=uint8_t(regs.r[rd]&0xFF); assert(res==0x01); assert(f.carry); assert(!f.negative);
    }
    // SHLR.B #2: 0x02 -> 0x00, C=1, Z=1
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=1; regs.r[rd]=0x0002; one_step(emu,0x1000,0x11, uint8_t((0x4<<4)|rd));
        uint8_t res=uint8_t(regs.r[rd]&0xFF); assert(res==0x00); assert(f.carry); assert(f.zero);
    }
    // ROTL.B 1-bit: 0x81 -> 0x03, C=1, N=0
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=0; regs.r[rd]=0x0081; one_step(emu,0x1000,0x12, uint8_t((0x8<<4)|rd));
        uint8_t res=uint8_t(regs.r[rd]&0xFF); assert(res==0x03); assert(f.carry); assert(!f.negative); assert(!f.overflow);
    }
    // ROTR.B #2: 0x02 -> 0x80, C=1, N=1
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=5; regs.r[rd]=0x0002; one_step(emu,0x1000,0x13, uint8_t((0xC<<4)|rd));
        uint8_t res=uint8_t(regs.r[rd]&0xFF); assert(res==0x80); assert(f.carry); assert(f.negative);
    }
    // SHLL.W #2: 0x4000 -> 0x0000, C=1
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t rd=4; regs.r[rd]=0x4000; one_step(emu,0x1000,0x10, uint8_t((0x5<<4)|rd));
        uint16_t res=uint16_t(regs.r[rd]&0xFFFF); assert(res==0x0000); assert(f.carry); assert(f.zero);
    }
    // SHLR.L 1-bit: 0x00000003 -> 0x00000001, C=1, N=0
    {
        H8S2350Emulator emu; emu.reset(); auto& regs=emu.getRegisters(); auto& f=emu.getFlags();
        uint8_t erd=6; regs.er[erd]=0x00000003u; one_step(emu,0x1000,0x11, 0x30, true, erd);
        uint32_t res=regs.er[erd]; assert(res==0x00000001u); assert(f.carry); assert(!f.negative);
    }

    std::puts("[SHIFT-MATRIX] all checks passed.");
    return 0;
}

