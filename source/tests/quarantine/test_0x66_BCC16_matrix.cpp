#include "../core/h8s2350_emulator.h"
#include <iostream>
#include <cassert>

using namespace MS2000;

// BCC16 Test Matrix: Branch if Carry Clear (BHS - Branch if Higher or Same)
// Condition: C == 0
// Format: 0x00 0x66 disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

bool test_bcc16_taken_positive() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BCC16 0x00 0x66 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x66);  // BCC16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BCC condition: C == 0
    // Set C=0 -> taken
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
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+256=0x001104
    if (pc_after == 0x001104 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BCC16 taken positive: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (3 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BCC16 taken positive: expected 0x001104, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bcc16_taken_negative() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BCC16 0x00 0x66 0xFF 0x80 -> disp=-128
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x66);  // BCC16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0xFF);  // disp_hi = -128 (signed)
    emu.writeByte(0x001003, 0x80);  // disp_lo
    
    // BCC condition: C == 0
    // Set C=0 -> taken
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
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004-128=0x000F84
    if (pc_after == 0x000F84 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BCC16 taken negative: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (3 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BCC16 taken negative: expected 0x000F84, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bcc16_not_taken() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BCC16 0x00 0x66 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x66);  // BCC16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BCC condition: C == 0
    // Set C=1 -> not taken
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
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (not taken)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BCC16 not taken: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (2 cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BCC16 not taken: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bcc16_edge_case() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BCC16 0x00 0x66 0x00 0x50 -> disp=+80
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x66);  // BCC16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x00);  // disp_hi = +80
    emu.writeByte(0x001003, 0x50);  // disp_lo
    
    // BCC condition: C == 0
    // Set C=0 with other flags set -> still taken (C only matters)
    auto& flags = emu.getFlags();
    flags.zero = true;       // Should not affect
    flags.negative = true;   // Should not affect
    flags.overflow = true;   // Should not affect
    flags.carry = false;     // Only C matters for BCC16
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+80=0x001054
    if (pc_after == 0x001054 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BCC16 edge case: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (C=0 overrides other flags)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BCC16 edge case: expected 0x001054, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

int main() {
    std::cout << "[BCC16-MATRIX] Starting BCC16 test matrix..." << std::endl;
    
    bool all_passed = true;
    all_passed &= test_bcc16_taken_positive();
    all_passed &= test_bcc16_taken_negative();
    all_passed &= test_bcc16_not_taken();
    all_passed &= test_bcc16_edge_case();
    
    if (all_passed) {
        std::cout << "[BCC16-MATRIX] All BCC16 test cases passed! ✅" << std::endl;
        std::cout << "[BCC16-INFO] BCC condition: C == 0 - Branch if Carry Clear (BHS)" << std::endl;
        std::cout << "[BCC16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken" << std::endl;
        std::cout << "[BCC16-INFO] Target: next_pc + signed16_displacement (24-bit masked)" << std::endl;
        return 0;
    } else {
        std::cout << "[BCC16-MATRIX] Some BCC16 test cases failed! ❌" << std::endl;
        return 1;
    }
}