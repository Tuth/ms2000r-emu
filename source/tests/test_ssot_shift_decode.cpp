#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cstdio>
#include <cassert>

using namespace H8S;
using MS2000::H8S2350Emulator;

static void write_insn(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1, bool has_b2=false, uint8_t b2=0){
    auto& regs = emu.getRegisters();
    regs.pc = pc;
    emu.writeByte(pc+0, b0);
    emu.writeByte(pc+1, b1);
    if (has_b2) emu.writeByte(pc+2, b2);
}

static void assert_pc_invariant(H8S2350Emulator& emu){
    uint32_t pc = emu.lastExecPC();
    uint32_t st = emu.lastExecStart();
    uint8_t  sz = emu.lastExecSize();
    uint8_t  pr = emu.lastExecPrimary();
    if (pc != st + sz) {
        std::printf("[ASSERT-CTX] pc=%06X start=%06X size=%u primary=%02X\n", pc, st, (unsigned)sz, pr);
    }
    assert(pc == st + sz);
}

int main(){
    std::puts("=== SSoT SHIFT decode smoke ===");
    // .B/.W 2-byte forms
    {
        H8S2350Emulator emu; emu.reset();
        const uint8_t primaries[] = { Shift::SHLL, Shift::SHLR, Shift::ROTL, Shift::ROTR };
        const uint8_t subs_bw[]   = { 0x0, 0x4, 0x1, 0x5, 0x8, 0xC, 0x9, 0xD };
        for (auto p: primaries){
            for (uint8_t rd=0; rd<8; ++rd){
                for (auto sub: subs_bw){
                    uint32_t pc0 = 0x001000;
                    write_insn(emu, pc0, p, static_cast<uint8_t>((sub<<4)|rd));
                    emu.step();
                    assert_pc_invariant(emu);
                }
            }
        }
    }
    // .L 3-byte forms
    {
        H8S2350Emulator emu; emu.reset();
        struct C { uint8_t p, sub; } cases[] = {
            {Shift::SHLL, 0x2}, {Shift::SHLL, 0x6},
            {Shift::SHLR, 0x3}, {Shift::SHLR, 0x7},
            {Shift::ROTL, 0xB}, {Shift::ROTL, 0xF},
            {Shift::ROTR, 0xB}, {Shift::ROTR, 0xF},
        };
        for (auto c: cases){
            for (uint8_t erd=0; erd<8; ++erd){
                uint32_t pc0 = 0x002000;
                write_insn(emu, pc0, c.p, static_cast<uint8_t>((c.sub<<4)|0x0), true, erd);
                emu.step();
                assert_pc_invariant(emu);
                // Regression: .L must decode to size 3
                assert(emu.lastExecSize() == 3);
            }
        }
    }
    std::puts("[SMOKE-OK] SHIFT primaries decode across .B/.W/.L");
    return 0;
}
