#include "../core/h8s2350_emulator.h"
#include <iostream>
#include <cassert>

using namespace MS2000;

// BMI16 Test Matrix: Branch if Minus (P1.5 version)
// Condition: N == 1
// Format: 0x00 0x6B disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

bool test_bmi16_taken_positive() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BMI16 0x00 0x6B 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x6B);  // BMI16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BMI condition: N == 1
    // Set N=1 -> taken
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = true;
    flags.overflow = false;
    flags.carry = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+256=0x001104
    if (pc_after == 0x001104 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BMI16 taken positive: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (3 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BMI16 taken positive: expected 0x001104, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bmi16_taken_negative() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BMI16 0x00 0x6B 0xFF 0x80 -> disp=-128
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x6B);  // BMI16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0xFF);  // disp_hi = -128 (signed)
    emu.writeByte(0x001003, 0x80);  // disp_lo
    
    // BMI condition: N == 1
    // Set N=1 -> taken
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = true;
    flags.overflow = false;
    flags.carry = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004-128=0x000F84
    if (pc_after == 0x000F84 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BMI16 taken negative: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (3 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BMI16 taken negative: expected 0x000F84, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bmi16_not_taken() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BMI16 0x00 0x6B 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x6B);  // BMI16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BMI condition: N == 1
    // Set N=0 -> not taken
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
        std::cout << "[TEST-PASS] BMI16 not taken: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (2 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BMI16 not taken: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bmi16_edge_case() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BMI16 0x00 0x6B 0x00 0x50 -> disp=+80
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x6B);  // BMI16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x00);  // disp_hi = +80
    emu.writeByte(0x001003, 0x50);  // disp_lo
    
    // BMI condition: N == 1
    // Set N=1 with other flags set -> still taken (N only matters)
    auto& flags = emu.getFlags();
    flags.zero = true;       // Should not affect
    flags.negative = true;   // Only N matters for BMI16
    flags.overflow = true;   // Should not affect
    flags.carry = true;      // Should not affect
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+80=0x001054
    if (pc_after == 0x001054 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BMI16 edge case: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (N=1 overrides other flags)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BMI16 edge case: expected 0x001054, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

int main() {
    std::cout << "[BMI16-MATRIX] Starting BMI16 test matrix..." << std::endl;
    
    bool all_passed = true;
    all_passed &= test_bmi16_taken_positive();
    all_passed &= test_bmi16_taken_negative();
    all_passed &= test_bmi16_not_taken();
    all_passed &= test_bmi16_edge_case();
    
    if (all_passed) {
        std::cout << "[BMI16-MATRIX] All BMI16 test cases passed! ✅" << std::endl;
        std::cout << "[BMI16-INFO] BMI condition: N == 1 - Branch if Minus (P1.5)" << std::endl;
        std::cout << "[BMI16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken" << std::endl;
        std::cout << "[BMI16-INFO] Target: next_pc + signed16_displacement (24-bit masked)" << std::endl;
        return 0;
    } else {
        std::cout << "[BMI16-MATRIX] Some BMI16 test cases failed! ❌" << std::endl;
        return 1;
    }
}