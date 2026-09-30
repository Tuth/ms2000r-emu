#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_contracts.h"
#include <cassert>
#include <cstdio>

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
    std::puts("=== SSoT ROTX/SHAL decode smoke ===");
    // ROTXL/ROTXR .B/.W (2-byte) and .L (3-byte) forms
    {
        H8S2350Emulator emu; emu.reset();
        const uint8_t primaries[] = { 0x12, 0x13 }; // ROTXL/ROTXR
        const uint8_t subs_bw[]   = { 0x0, 0x4, 0x1, 0x5 }; // 1-bit, #2 for B/W
        for (auto p: primaries){
            for (uint8_t rd=0; rd<8; ++rd){
                for (auto sub: subs_bw){
                    uint32_t pc0 = 0x001000;
                    write_insn(emu, pc0, p, static_cast<uint8_t>((sub<<4)|rd));
                    emu.step();
                    assert_pc_invariant(emu);
                }
            }
            // .L
            for (uint8_t erd=0; erd<8; ++erd){
                uint32_t pc0 = 0x002000;
                write_insn(emu, pc0, p, 0x30, true, erd); // 1-bit .L
                emu.step();
                assert_pc_invariant(emu);
                pc0 += 0x10;
                write_insn(emu, pc0, p, 0x70, true, erd); // #2 .L
                emu.step();
                assert_pc_invariant(emu);
            }
        }
    }
    // SHAL .B/.W/.L forms under primary 0x10 using distinct sub-nibbles
    {
        H8S2350Emulator emu; emu.reset();
        for (uint8_t rd=0; rd<8; ++rd){
            // .B 1-bit / #2
            uint32_t pc0 = 0x003000;
            write_insn(emu, pc0, 0x10, static_cast<uint8_t>((0x8u<<4)|rd)); emu.step();
            assert_pc_invariant(emu);
            pc0 += 0x10;
            write_insn(emu, pc0, 0x10, static_cast<uint8_t>((0xCu<<4)|rd)); emu.step();
            assert_pc_invariant(emu);
            // .W 1-bit / #2
            pc0 += 0x10;
            write_insn(emu, pc0, 0x10, static_cast<uint8_t>((0x9u<<4)|rd)); emu.step();
            assert_pc_invariant(emu);
            pc0 += 0x10;
            write_insn(emu, pc0, 0x10, static_cast<uint8_t>((0xDu<<4)|rd)); emu.step();
            assert_pc_invariant(emu);
        }
        for (uint8_t erd=0; erd<8; ++erd){
            uint32_t pc0 = 0x004000;
            write_insn(emu, pc0, 0x10, 0xB0, true, erd); emu.step();
            assert_pc_invariant(emu);
            pc0 += 0x10;
            write_insn(emu, pc0, 0x10, 0xF0, true, erd); emu.step();
            assert_pc_invariant(emu);
        }
    }
    std::puts("[SMOKE-OK] ROTX/SHAL decode across .B/.W/.L");
    return 0;
}
