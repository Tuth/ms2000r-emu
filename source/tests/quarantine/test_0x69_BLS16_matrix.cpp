#include "../core/h8s2350_emulator.h"
#include <iostream>
#include <cassert>

using namespace MS2000;

// BLS16 Test Matrix: Branch if Lower or Same
// Condition: C == 1 || Z == 1
// Format: 0x00 0x69 disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

bool test_bls16_taken_positive() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLS16 0x00 0x69 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x69);  // BLS16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BLS condition: C == 1 || Z == 1
    // Set C=1 -> taken (OR condition, only one needs to be true)
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = false;
    flags.overflow = false;
    flags.carry = true;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+256=0x001104
    if (pc_after == 0x001104 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BLS16 taken positive: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (3 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLS16 taken positive: expected 0x001104, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bls16_taken_negative() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLS16 0x00 0x69 0xFF 0x80 -> disp=-128
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x69);  // BLS16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0xFF);  // disp_hi = -128 (signed)
    emu.writeByte(0x001003, 0x80);  // disp_lo
    
    // BLS condition: C == 1 || Z == 1
    // Set Z=1 -> taken (OR condition, only one needs to be true)
    auto& flags = emu.getFlags();
    flags.zero = true;
    flags.negative = false;
    flags.overflow = false;
    flags.carry = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004-128=0x000F84
    if (pc_after == 0x000F84 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BLS16 taken negative: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (3 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLS16 taken negative: expected 0x000F84, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bls16_not_taken() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLS16 0x00 0x69 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x69);  // BLS16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BLS condition: C == 1 || Z == 1
    // Set C=0, Z=0 -> not taken (both conditions false)
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = false;
    flags.overflow = false;
    flags.carry = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (not taken)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BLS16 not taken: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (2 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLS16 not taken: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bls16_edge_case() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLS16 0x00 0x69 0x00 0x50 -> disp=+80
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x69);  // BLS16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x00);  // disp_hi = +80
    emu.writeByte(0x001003, 0x50);  // disp_lo
    
    // BLS condition: C == 1 || Z == 1
    // Set C=1, Z=1 -> taken (both conditions true, OR still satisfied)
    auto& flags = emu.getFlags();
    flags.zero = true;       // Z=1 is sufficient
    flags.negative = true;   // Should not affect
    flags.overflow = true;   // Should not affect
    flags.carry = true;      // C=1 is also sufficient
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+80=0x001054
    if (pc_after == 0x001054 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BLS16 edge case: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (C=1 || Z=1, both true)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLS16 edge case: expected 0x001054, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

int main() {
    std::cout << "[BLS16-MATRIX] Starting BLS16 test matrix..." << std::endl;
    
    bool all_passed = true;
    all_passed &= test_bls16_taken_positive();
    all_passed &= test_bls16_taken_negative();
    all_passed &= test_bls16_not_taken();
    all_passed &= test_bls16_edge_case();
    
    if (all_passed) {
        std::cout << "[BLS16-MATRIX] All BLS16 test cases passed! ✅" << std::endl;
        std::cout << "[BLS16-INFO] BLS condition: C == 1 || Z == 1 - Branch if Lower or Same" << std::endl;
        std::cout << "[BLS16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken" << std::endl;
        std::cout << "[BLS16-INFO] Target: next_pc + signed16_displacement (24-bit masked)" << std::endl;
        return 0;
    } else {
        std::cout << "[BLS16-MATRIX] Some BLS16 test cases failed! ❌" << std::endl;
        return 1;
    }
}