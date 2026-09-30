#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void run2(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1){
    auto& regs = emu.getRegisters();
    regs.pc = pc;
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
    emu.step();
}

int main(){
    std::puts("[ADDX-MATRIX] start...");

    // Carry-in path: C=1, dst=0xFF, src=0x00 -> res=0x00, C=1, H=1, Z=1, N=0, V=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        // rd=0 (R0L), rs=0 (R0L irrelevant since src comes from rs)
        r.rl[0] = 0xFF; r.rl[1] = 0x00; f.carry = true;
        run2(emu, 0x1000, 0x0E, static_cast<uint8_t>(((1u|8u)<<4)|(0u|8u)));
        assert(r.rl[0] == 0x00);
        assert(f.carry && f.half_carry && f.zero && !f.negative && !f.overflow);
    }
    // Overflow + half-carry: C=0, dst=0x7F, src=0x01 -> res=0x80, H=1, V=1, N=1, C=0, Z=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        r.rl[2] = 0x7F; r.rl[3] = 0x01; f.carry = false;
        run2(emu, 0x1000, 0x0E, static_cast<uint8_t>(((3u|8u)<<4)|(2u|8u)));
        assert(r.rl[2] == 0x80);
        assert(!f.carry && f.half_carry && !f.zero && f.negative && f.overflow);
    }
    // Neg + no overflow: C=0, dst=0x55, src=0xAA -> res=0xFF, N=1, V=0, C=0, H=0, Z=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        r.rl[4] = 0x55; r.rl[5] = 0xAA; f.carry = false;
        run2(emu, 0x1000, 0x0E, static_cast<uint8_t>(((5u|8u)<<4)|(4u|8u)));
        assert(r.rl[4] == 0xFF);
        assert(!f.carry && !f.half_carry && !f.zero && f.negative && !f.overflow);
    }
    // Big carry: C=0, dst=0x80, src=0x80 -> res=0x00, C=1, V=1, N=0, Z=1, H=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        r.rl[6] = 0x80; r.rl[7] = 0x80; f.carry = false;
        run2(emu, 0x1000, 0x0E, static_cast<uint8_t>(((7u|8u)<<4)|(6u|8u)));
        assert(r.rl[6] == 0x00);
        assert(f.carry && !f.half_carry && f.zero && !f.negative && f.overflow);
    }
    // IMM8 form mirror: rd=R1H (idx=9), imm=0x01, dst=0x7F -> res=0x81, check flags
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        r.rh[1] = 0x7F; f.carry = 0;
        auto pc = 0x2000; auto rd_idx = 1u; // R1H   // BUG109: register field per HM Appendix A.2 legend (rendered p.807): 0-7 = RnH, 8-15 = RnL.
        r.pc = pc; emu.writeByte(pc+0, static_cast<uint8_t>(0x90 | rd_idx)); emu.writeByte(pc+1, 0x01);
        emu.step();
        assert(r.rh[1] == 0x80);
        assert(!f.carry && f.half_carry && !f.zero && f.negative && f.overflow);
    }

    std::puts("[ADDX-MATRIX] all checks passed.");
    return 0;
}

