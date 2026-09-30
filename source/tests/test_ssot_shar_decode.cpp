#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_instructions.h"
#include "../core/h8s2350_shar_contracts.h"
#include <cassert>
#include <cstdio>

using namespace MS2000;
using namespace H8S::SHAR;

static void write_rr(H8S2350Emulator& emu, uint32_t pc, uint8_t primary, uint8_t rs, uint8_t rd) {
    emu.writeByte(pc + 0, primary);
    emu.writeByte(pc + 1, static_cast<uint8_t>((rs << 4) | rd));
}

int main() {
    std::printf("=== SSoT SHAR decode smoke ===\n");
    H8S2350InstructionDecoder dec;
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x001000;
        for (uint8_t r = 0; r < 8; ++r) {
            write_rr(emu, pc, PRIMARY_B, r, r);
            auto ins = dec.decode(emu, pc);
            assert(ins.opcode == PRIMARY_B && ins.size == 2 && "SHAR.B decode failed");
        }
        for (uint8_t r = 0; r < 8; ++r) {
            write_rr(emu, pc, PRIMARY_W, r, r);
            auto ins = dec.decode(emu, pc);
            assert(ins.opcode == PRIMARY_W && ins.size == 2 && "SHAR.W decode failed");
        }
        for (uint8_t r = 0; r < 8; ++r) {
            write_rr(emu, pc, PRIMARY_L, r, r);
            auto ins = dec.decode(emu, pc);
            assert(ins.opcode == PRIMARY_L && ins.size == 2 && "SHAR.L decode failed");
        }
        std::printf("[SMOKE-OK] SHAR primaries decode across all regs.\n");
    }
    // Negative decode: invalid op2 high bits (mirrors SUBX guard style)
    {
        H8S2350Emulator emu; emu.reset();
        const uint32_t pc = 0x002000;
        write_rr(emu, pc, PRIMARY_B, 8, 8); // invalid (upper bit set)
        auto ins = dec.decode(emu, pc);
        // Must not accept as SHAR.B valid decode (size 2 and opcode match)
        assert(!(ins.opcode == PRIMARY_B && ins.size == 2) && "Invalid SHAR should not decode");
        std::printf("[SMOKE-OK] Negative decode guard blocked invalid SHAR.\n");
    }
    std::printf("All SHAR decode smoke tests passed.\n");
    return 0;
}

