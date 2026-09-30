#include "../core/h8s2350_emulator.h"
#include <iostream>
#include <cassert>

using namespace MS2000;

// BGE16 Test Matrix: Branch if Greater or Equal
// Condition: (N == V)
// Format: 0x00 0x7C disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

bool test_bge16_taken_positive() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGE16 0x00 0x7C 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7C);  // BGE16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BGE condition: (N == V)
    // Set N=0, V=0 -> (0 == 0) = TAKEN
    auto& flags = emu.getFlags();
    flags.negative = false;
    flags.overflow = false;
    flags.zero = false;  // Z doesn't matter for BGE
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+256=0x001104
    if (pc_after == 0x001104 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BGE16 taken positive: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGE16 taken positive: expected 0x001104, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bge16_taken_negative() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGE16 0x00 0x7C 0xFF 0x80 -> disp=-128
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7C);  // BGE16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0xFF);  // disp_hi = -128 (signed)
    emu.writeByte(0x001003, 0x80);  // disp_lo
    
    // BGE condition: (N == V)
    // Set N=1, V=1 -> (1 == 1) = TAKEN
    auto& flags = emu.getFlags();
    flags.negative = true;
    flags.overflow = true;
    flags.zero = false;  // Z doesn't matter for BGE
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004-128=0x000F84
    if (pc_after == 0x000F84 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BGE16 taken negative: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGE16 taken negative: expected 0x000F84, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bge16_not_taken() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGE16 0x00 0x7C 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7C);  // BGE16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BGE condition: (N == V)
    // Set N=1, V=0 -> (1 == 0) = NOT TAKEN
    auto& flags = emu.getFlags();
    flags.negative = true;
    flags.overflow = false;
    flags.zero = false;  // Z doesn't matter for BGE
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (not taken)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BGE16 not taken: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGE16 not taken: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bge16_z_independence() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGE16 0x00 0x7C 0x00 0x50 -> disp=+80
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7C);  // BGE16 opcode (H8S/2600)  
    emu.writeByte(0x001002, 0x00);  // disp_hi = +80
    emu.writeByte(0x001003, 0x50);  // disp_lo
    
    // BGE condition: (N == V) - Z flag should NOT affect BGE
    // Set N=0, V=0, Z=1 -> (0 == 0) = TAKEN (Z=1 should not block)
    auto& flags = emu.getFlags();
    flags.negative = false;
    flags.overflow = false;
    flags.zero = true;  // Z=1 should NOT block BGE (unlike BLT/BGT)
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+80=0x001054 (taken despite Z=1)
    if (pc_after == 0x001054 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BGE16 Z independence: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (Z=1 does not block N==V)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGE16 Z independence: expected 0x001054, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

int main() {
    std::cout << "[BGE16-MATRIX] Starting BGE16 test matrix..." << std::endl;
    
    bool all_passed = true;
    all_passed &= test_bge16_taken_positive();
    all_passed &= test_bge16_taken_negative();
    all_passed &= test_bge16_not_taken();
    all_passed &= test_bge16_z_independence();
    
    if (all_passed) {
        std::cout << "[BGE16-MATRIX] All BGE16 test cases passed! ✅" << std::endl;
        std::cout << "[BGE16-INFO] BGE condition: (N == V) - Branch if Greater or Equal" << std::endl;
        std::cout << "[BGE16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken" << std::endl;
        std::cout << "[BGE16-INFO] Target: next_pc + signed16_displacement (24-bit masked)" << std::endl;
        return 0;
    } else {
        std::cout << "[BGE16-MATRIX] Some BGE16 test cases failed! ❌" << std::endl;
        return 1;
    }
}