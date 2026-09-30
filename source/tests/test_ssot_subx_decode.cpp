// SSoT smoke test for SUBX decode paths

// (old duplicate content removed)
// SSoT smoke test for SUBX decode paths

#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_instructions.h"
#include <cassert>
#include <cstdio>

using namespace MS2000;

static void write_rr(H8S2350Emulator& emu, uint32_t pc, uint8_t rs, uint8_t rd) {
    emu.writeByte(pc + 0, 0x1E);
    emu.writeByte(pc + 1, static_cast<uint8_t>((rs << 4) | rd));
}

static void write_imm(H8S2350Emulator& emu, uint32_t pc, uint8_t rd, uint8_t imm) {
    emu.writeByte(pc + 0, static_cast<uint8_t>(0xB0 | (rd & 7)));
    emu.writeByte(pc + 1, imm);
}

int main() {
    std::printf("=== SSoT SUBX decode smoke ===\n");
    H8S2350InstructionDecoder dec;
    // RR across pairs
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x005000;
        for (uint8_t rs = 0; rs < 8; ++rs) {
            for (uint8_t rd = 0; rd < 8; ++rd) {
                write_rr(emu, pc, rs, rd);
                auto ins = dec.decode(emu, pc);
                assert(ins.size == 2 && ins.opcode == 0x1E);
            }
        }
        std::printf("[SMOKE-OK] SUBX RR decode across all pairs.\n");
    }
    // IMM across rd
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x006000;
        for (uint8_t rd = 0; rd < 8; ++rd) {
            write_imm(emu, pc, rd, 0x7F);
            auto ins = dec.decode(emu, pc);
            assert(ins.size == 2 && (ins.opcode & 0xF8) == 0xB0);
        }
        std::printf("[SMOKE-OK] SUBX IMM8 decode across all rd.\n");
    }
    std::printf("All SUBX decode smoke tests passed.\n");
    return 0;
}
