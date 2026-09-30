#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void run(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1, bool has_b2=false, uint8_t b2=0){
    auto& regs = emu.getRegisters();
    regs.pc = pc;
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
    if (has_b2) emu.writeByte(pc+2, b2);
    emu.step();
}

int main(){
    std::puts("[ROTX/SHAL-MATRIX] start...");

    // ROTXL.B 1-bit: before 0x80, C_in=0 -> after 0x00, C=1, Z=1, N=0, V=0
    {
        H8S2350Emulator emu; emu.reset(); auto &regs=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=0; regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | 0x80u; f.carry = false;
        run(emu, 0x1000, 0x12, uint8_t((0x0u<<4)|rd));
        uint8_t res = uint8_t(regs.r[rd] & 0xFFu);
        assert(res==0x00u && f.carry && f.zero && !f.negative && !f.overflow);
    }
    // ROTXL.B #2: before 0x40, C_in=1 -> C_out=old bit6=1
    {
        H8S2350Emulator emu; emu.reset(); auto &regs=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=1; regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | 0x40u; f.carry = true;
        run(emu, 0x1000, 0x12, uint8_t((0x4u<<4)|rd));
        assert(f.carry==true);
    }
    // ROTXR.B 1-bit: before 0x01, C_in=0 -> after 0x00, C=1, Z=1, N=0
    {
        H8S2350Emulator emu; emu.reset(); auto &regs=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=2; regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | 0x01u; f.carry = false;
        run(emu, 0x1000, 0x13, uint8_t((0x0u<<4)|rd));
        uint8_t res = uint8_t(regs.r[rd] & 0xFFu);
        assert(res==0x00u && f.carry && f.zero && !f.negative);
    }
    // SHAL.B 1-bit: before 0x80 -> after 0x00, C=1, Z=1, N=0, V=1 (Dm xor Dm-1)
    {
        H8S2350Emulator emu; emu.reset(); auto &regs=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=3; regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | 0x80u;
        run(emu, 0x1000, 0x10, uint8_t((0x8u<<4)|rd));
        uint8_t res = uint8_t(regs.r[rd] & 0xFFu);
        assert(res==0x00u && f.carry && f.zero && !f.negative && f.overflow);
    }
    // SHAL.B 1-bit: before 0xC0 -> V=0 (MSB==next-MSB)
    {
        H8S2350Emulator emu; emu.reset(); auto &regs=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=4; regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | 0xC0u;
        run(emu, 0x1000, 0x10, uint8_t((0x8u<<4)|rd));
        assert(!f.overflow);
    }

    std::puts("[ROTX/SHAL-MATRIX] all checks passed.");
    return 0;
}

