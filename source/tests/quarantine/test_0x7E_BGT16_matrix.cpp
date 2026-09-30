#include "../core/h8s2350_emulator.h"
#include <iostream>
#include <cassert>

using namespace MS2000;

// BGT16 Test Matrix: Branch if Greater Than
// Condition: !Z && (N == V)
// Format: 0x00 0x7E disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

bool test_bgt16_taken_positive() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGT16 0x00 0x7E 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7E);  // BGT16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BGT condition: !Z && (N == V)
    // Set Z=0, N=0, V=0 -> !0 && (0 == 0) = true && true = TAKEN
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = false;
    flags.overflow = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+256=0x001104
    if (pc_after == 0x001104 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BGT16 taken positive: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGT16 taken positive: expected 0x001104, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bgt16_not_taken_z() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGT16 0x00 0x7E 0x01 0x00 -> disp=+256
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7E);  // BGT16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0x01);  // disp_hi = +256
    emu.writeByte(0x001003, 0x00);  // disp_lo
    
    // BGT condition: !Z && (N == V)
    // Set Z=1, N=0, V=0 -> !1 && (0 == 0) = false && true = NOT TAKEN (Z blocks)
    auto& flags = emu.getFlags();
    flags.zero = true;   // Z=1 should block the branch
    flags.negative = false;
    flags.overflow = false;
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (not taken due to Z=1)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BGT16 not taken Z=1: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGT16 not taken Z=1: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bgt16_not_taken_nv() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGT16 0x00 0x7E 0xFF 0x80 -> disp=-128
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7E);  // BGT16 opcode (H8S/2600)
    emu.writeByte(0x001002, 0xFF);  // disp_hi = -128 (signed)
    emu.writeByte(0x001003, 0x80);  // disp_lo
    
    // BGT condition: !Z && (N == V)
    // Set Z=0, N=1, V=0 -> !0 && (1 == 0) = true && false = NOT TAKEN (N!=V blocks)
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = true;
    flags.overflow = false;  // N!=V should block the branch
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 (not taken due to N!=V)
    if (pc_after == 0x001004 && (cycles_after - cycles_before) == 2) {
        std::cout << "[TEST-PASS] BGT16 not taken N!=V: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (" << std::dec << (cycles_after - cycles_before) << " cycles)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGT16 not taken N!=V: expected 0x001004, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

bool test_bgt16_taken_negative() {
    H8S2350Emulator emu;
    emu.reset();
    
    // Set up BGT16 0x00 0x7E 0x00 0x50 -> disp=+80
    emu.writeByte(0x001000, 0x00);
    emu.writeByte(0x001001, 0x7E);  // BGT16 opcode (H8S/2600)  
    emu.writeByte(0x001002, 0x00);  // disp_hi = +80
    emu.writeByte(0x001003, 0x50);  // disp_lo
    
    // BGT condition: !Z && (N == V)
    // Set Z=0, N=1, V=1 -> !0 && (1 == 1) = true && true = TAKEN
    auto& flags = emu.getFlags();
    flags.zero = false;
    flags.negative = true;
    flags.overflow = true;  // N==V should allow branch
    
    emu.setProgramCounter(0x001000);
    const uint32_t cycles_before = emu.getCycles();
    
    try { emu.step(); } catch (...) { /* void return */ }
    
    const uint32_t cycles_after = emu.getCycles();
    const uint32_t pc_after = emu.getProgramCounter();
    
    // Expected: PC=0x001000 -> next_pc=0x001004 -> target=0x001004+80=0x001054 (taken)
    if (pc_after == 0x001054 && (cycles_after - cycles_before) == 3) {
        std::cout << "[TEST-PASS] BGT16 taken N==V: 0x001000 -> 0x" << std::hex 
                  << pc_after << " (N==V allows branch)" << std::endl;
        return true;
    }
    
    std::cout << "[TEST-FAIL] BGT16 taken N==V: expected 0x001054, got 0x" << std::hex << pc_after << std::endl;
    return false;
}

int main() {
    std::cout << "[BGT16-MATRIX] Starting BGT16 test matrix..." << std::endl;
    
    bool all_passed = true;
    all_passed &= test_bgt16_taken_positive();
    all_passed &= test_bgt16_not_taken_z();
    all_passed &= test_bgt16_not_taken_nv();
    all_passed &= test_bgt16_taken_negative();
    
    if (all_passed) {
        std::cout << "[BGT16-MATRIX] All BGT16 test cases passed! ✅" << std::endl;
        std::cout << "[BGT16-INFO] BGT condition: !Z && (N == V) - Branch if Greater Than" << std::endl;
        std::cout << "[BGT16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken" << std::endl;
        std::cout << "[BGT16-INFO] Target: next_pc + signed16_displacement (24-bit masked)" << std::endl;
        return 0;
    } else {
        std::cout << "[BGT16-MATRIX] Some BGT16 test cases failed! ❌" << std::endl;
        return 1;
    }
}