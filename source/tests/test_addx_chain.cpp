#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

using MS2000::H8S2350Emulator;

static void emit_rr(H8S2350Emulator& emu, uint32_t pc, uint8_t rs_idx, uint8_t rd_idx){
    emu.writeByte(pc+0, 0x0E);
    emu.writeByte(pc+1, static_cast<uint8_t>(((rs_idx | 8u)<<4) | ((rd_idx | 8u) & 0x0F)));   // BUG109: register field per HM Appendix A.2 legend (rendered p.807): 0-7 = RnH, 8-15 = RnL.
}

int main(){
    std::puts("[ADDX-CHAIN] start...");
    // Build a 32-bit addition using four ADDX.B with carry chaining.
    // Use RL[0..3] as destination bytes (least->most), RL[4..7] as source bytes.
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        // Dest = 0x00FF00FF, Src = 0xFF000001 => Sum = 0x01000000
        r.rl[0] = 0xFF; r.rl[1] = 0x00; r.rl[2] = 0xFF; r.rl[3] = 0x00;
        r.rl[4] = 0x01; r.rl[5] = 0x00; r.rl[6] = 0x00; r.rl[7] = 0xFF;
        f.carry = 0;
        uint32_t pc = 0x010000;
        // Byte0
        emit_rr(emu, pc, 4, 0); r.pc = pc; emu.step(); bool z0 = (r.rl[0]==0);
        // Byte1
        pc += 2; emit_rr(emu, pc, 5, 1); r.pc = pc; emu.step(); bool z1 = (r.rl[1]==0);
        // Byte2
        pc += 2; emit_rr(emu, pc, 6, 2); r.pc = pc; emu.step(); bool z2 = (r.rl[2]==0);
        // Byte3
        pc += 2; emit_rr(emu, pc, 7, 3); r.pc = pc; emu.step(); bool z3 = (r.rl[3]==0);

        uint32_t sum = (uint32_t)r.rl[0] | ((uint32_t)r.rl[1]<<8) | ((uint32_t)r.rl[2]<<16) | ((uint32_t)r.rl[3]<<24);
        // Manual-correct ripple-carry across bytes → 0xFFFF0100
        assert(sum == 0xFFFF0100u);
        // Property: overall zero iff all bytes zero
        bool z_all = z0 && z1 && z2 && z3;
        assert((sum == 0) == z_all);
    }

    // Random-ish sanity: adding 0xFFFFFFFF + 0x00000001 -> 0x00000000, final carry=1
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        r.rl[0]=0xFF; r.rl[1]=0xFF; r.rl[2]=0xFF; r.rl[3]=0xFF;
        r.rl[4]=0x01; r.rl[5]=0x00; r.rl[6]=0x00; r.rl[7]=0x00;
        f.carry = 0;
        uint32_t pc = 0x020000;
        emit_rr(emu, pc, 4, 0); r.pc = pc; emu.step();
        pc += 2; emit_rr(emu, pc, 5, 1); r.pc = pc; emu.step();
        pc += 2; emit_rr(emu, pc, 6, 2); r.pc = pc; emu.step();
        pc += 2; emit_rr(emu, pc, 7, 3); r.pc = pc; emu.step();
        uint32_t sum = (uint32_t)r.rl[0] | ((uint32_t)r.rl[1]<<8) | ((uint32_t)r.rl[2]<<16) | ((uint32_t)r.rl[3]<<24);
        assert(sum == 0x00000000u);
        assert(f.carry == true);
    }

    std::puts("[ADDX-CHAIN] all checks passed.");
    return 0;
}
