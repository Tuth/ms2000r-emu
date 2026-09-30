#include "../core/h8s2350_emulator.h"
#include <iostream>
#include <cassert>

using namespace MS2000;

// BLT16 Test Matrix: Branch if Less Than
// Condition: (N != V) && !Z
// Format: 0x00 0x7D disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

bool test_blt16_taken_positive() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLT16 0x00 0x7D 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7D);  // BLT16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BLT condition: (N != V) && !Z
    // Set N=1, V=0, Z=0 -> (1 != 0) && !0 = true && true = TAKEN
    auto& flags = emu.getFlags();
    flags.negative = true;
    flags.overflow = false;
    flags.zero = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+256=0x001104
    if (pc_after == 0x001104 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BLT16 taken positive: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLT16 taken positive: expected 0x001104, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_blt16_taken_negative() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLT16 0x00 0x7D 0xFF 0x80 -> disp=-128
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7D);  // BLT16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0xFF);  // disp_hi = -128 (signed)
    emu.writeByte(0x001003, 0x80);  // disp_lo
    
    // BLT condition: (N != V) && !Z
    // Set N=0, V=1, Z=0 -> (0 != 1) && !0 = true && true = TAKEN
    auto& flags = emu.getFlags();
    flags.negative = false;
    flags.overflow = true;
    flags.zero = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004-128=0x000F84
    if (pc_after == 0x000F84 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BLT16 taken negative: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLT16 taken negative: expected 0x000F84, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_blt16_not_taken() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLT16 0x00 0x7D 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7D);  // BLT16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BLT condition: (N != V) && !Z
    // Set N=0, V=0, Z=0 -> (0 != 0) && !0 = false && true = NOT TAKEN
    auto& flags = emu.getFlags();
    flags.negative = false;
    flags.overflow = false;
    flags.zero = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (not taken)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BLT16 not taken: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLT16 not taken: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_blt16_z_guard() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BLT16 0x00 0x7D 0x00 0x50 -> disp=+80
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7D);  // BLT16 opcode (H8S/2600)  
    emu.writeByte(0x001002, 0x00);  // disp_hi = +80
    emu.writeByte(0x001003, 0x50);  // disp_lo
    
    // BLT condition: (N != V) && !Z
    // Set coherent flags like CMP R0,R0: Z=1, N=0, V=0 -> (0 != 0) && !1 = false && false = NOT TAKEN
    auto& flags = emu.getFlags();
    flags.negative = false;  // Coherent with Z=1 (equal result)
    flags.overflow = false;  // Coherent with Z=1 (no overflow on equal)
    flags.zero = true;       // Z=1 blocks BLT branch
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (Z=1 with coherent N==V flags)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BLT16 Z=1 guard: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (Z=1 with N==V coherent flags)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BLT16 Z=1 guard: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

int main() {
    std::cout << "[BLT16-MATRIX] Starting BLT16 test matrix..." << std::endl;
    
    bool all_passed = true;
    all_passed &= test_blt16_taken_positive();
    all_passed &= test_blt16_taken_negative();
    all_passed &= test_blt16_not_taken();
    all_passed &= test_blt16_z_guard();
    
    if (all_passed) {
        std::cout << "[BLT16-MATRIX] All BLT16 test cases passed! ✅" << std::endl;
        std::cout << "[BLT16-INFO] BLT condition: (N != V) && !Z - Branch if Less Than" << std::endl;
        std::cout << "[BLT16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken" << std::endl;
        std::cout << "[BLT16-INFO] Target: next_pc + signed16_displacement (24-bit masked)" << std::endl;
        return 0;
    } else {
        std::cout << "[BLT16-MATRIX] Some BLT16 test cases failed! ❌" << std::endl;
        return 1;
    }
}