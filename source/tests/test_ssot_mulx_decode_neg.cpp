#include "../core/h8s2350_emulator.h"
#include <cassert>

using MS2000::H8S2350Emulator;

static uint32_t step_one(H8S2350Emulator& emu, uint32_t pc) {
    emu.setProgramCounter(pc);
    emu.step();
    return emu.getProgramCounter();
}

int main(){
    // MULXU.W middle byte must be 0x00
    {
        H8S2350Emulator emu; emu.reset(); uint32_t pc = 0x001000;
        // Good: 0x52, rs, 0x00, erd -> size 4
        emu.writeByte(pc+0, 0x52);
        emu.writeByte(pc+1, 0x01);
        emu.writeByte(pc+2, 0x00);
        emu.writeByte(pc+3, 0x04);
        uint32_t next_pc_good = step_one(emu, pc);
        assert(next_pc_good == pc + 4);

        // Bad: middle byte != 0x00 -> must NOT advance by 4
        emu.writeByte(pc+0, 0x52);
        emu.writeByte(pc+1, 0x01);
        emu.writeByte(pc+2, 0xAB); // invalid
        emu.writeByte(pc+3, 0x04);
        uint32_t next_pc_bad = step_one(emu, pc);
        assert(next_pc_bad != pc + 4);
    }

    // MULXS.W middle byte must be 0x00
    {
        H8S2350Emulator emu; emu.reset(); uint32_t pc = 0x001200;
        // Good
        emu.writeByte(pc+0, 0x57);
        emu.writeByte(pc+1, 0x03);
        emu.writeByte(pc+2, 0x00);
        emu.writeByte(pc+3, 0x05);
        uint32_t next_pc_good = step_one(emu, pc);
        assert(next_pc_good == pc + 4);

        // Bad
        emu.writeByte(pc+0, 0x57);
        emu.writeByte(pc+1, 0x03);
        emu.writeByte(pc+2, 0x7E);
        emu.writeByte(pc+3, 0x05);
        uint32_t next_pc_bad = step_one(emu, pc);
        assert(next_pc_bad != pc + 4);
    }

    return 0;
}

