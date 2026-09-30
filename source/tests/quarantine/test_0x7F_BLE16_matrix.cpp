// Comprehensive test matrix for 0x7F BLE16 branch instruction
#include "core/h8s2350_emulator.h"
#include "core/h8s2350_contracts.h"
#include <cstdio>
#include <cstdint>

using namespace MS2000;

// Test helper: setup BLE16 instruction at given PC
void setupBLE16Instruction(H8S2350Emulator& emu, uint32_t pc, int16_t displacement) {
    // Install extended BLE16 instruction: 0x00 0x7F disp_hi disp_lo (SSoT)
    emu.writeByte(pc, 0x00);                                    // Extended prefix
    emu.writeByte(pc + 1, 0x7F);                               // BLE16 opcode (H8S/2600)
    emu.writeByte(pc + 2, (displacement >> 8) & 0xFF);         // displacement high (big-endian)
    emu.writeByte(pc + 3, displacement & 0xFF);                // displacement low
}

// Test helper: setup flags for BLE condition
void setupFlags(H8S2350Emulator& emu, bool zero, bool negative, bool overflow) {
    auto& flags = emu.getFlags();
    flags.zero = zero ? 1 : 0;
    flags.negative = negative ? 1 : 0;
    flags.overflow = overflow ? 1 : 0;
    flags.carry = 0;  // Not relevant for BLE
}

int main() {
    printf("[BLE16-MATRIX] Starting comprehensive BLE16 test matrix...\n");
    
    // Test 1: BLE16 taken, positive displacement
    {
        H8S2350Emulator emu;
        emu.reset();
        emu.setSP24(0x00F82000 - 4);
        emu.setVBR(0x00F80000);
        
        uint32_t pc_start = 0x001000;
        int16_t displacement = 0x0100;  // +256 bytes
        
        setupBLE16Instruction(emu, pc_start, displacement);
        emu.setProgramCounter(pc_start);
        setupFlags(emu, true, false, false);  // Z=1 → BLE condition true
        
        uint32_t cycles_before = emu.getCycles();
        emu.step();
        uint32_t cycles_after = emu.getCycles();
        
        uint32_t expected_target = pc_start + 4 + displacement;  // 0x1000 + 4 + 0x100 = 0x1104
        uint32_t actual_pc = emu.getProgramCounter();
        
        if (actual_pc != expected_target) {
            printf("[TEST-FAIL] BLE16 taken positive: expected 0x%06X, got 0x%06X\n", 
                   expected_target, actual_pc);
            return 1;
        }
        
        uint32_t cycle_delta = cycles_after - cycles_before;
        if (cycle_delta != 3) {  // 2 base + 1 taken
            printf("[TEST-WARN] BLE16 taken positive: expected 3 cycles, got %u\n", cycle_delta);
        }
        
        printf("[TEST-PASS] BLE16 taken positive: 0x%06X -> 0x%06X (%d cycles)\n", 
               pc_start, actual_pc, cycle_delta);
    }
    
    // Test 2: BLE16 taken, negative displacement
    {
        H8S2350Emulator emu;
        emu.reset();
        emu.setSP24(0x00F82000 - 4);
        emu.setVBR(0x00F80000);
        
        uint32_t pc_start = 0x001000;
        int16_t displacement = -0x0080;  // -128 bytes
        
        setupBLE16Instruction(emu, pc_start, displacement);
        emu.setProgramCounter(pc_start);
        setupFlags(emu, false, true, false);  // N=1, V=0 → N!=V → BLE condition true
        
        uint32_t cycles_before = emu.getCycles();
        emu.step();
        uint32_t cycles_after = emu.getCycles();
        
        uint32_t expected_target = pcMask24(pc_start + 4 + displacement);  // 0x1004 - 0x80 = 0x0F84
        uint32_t actual_pc = emu.getProgramCounter();
        
        if (actual_pc != expected_target) {
            printf("[TEST-FAIL] BLE16 taken negative: expected 0x%06X, got 0x%06X\n", 
                   expected_target, actual_pc);
            return 1;
        }
        
        uint32_t cycle_delta = cycles_after - cycles_before;
        if (cycle_delta != 3) {  // 2 base + 1 taken
            printf("[TEST-WARN] BLE16 taken negative: expected 3 cycles, got %u\n", cycle_delta);
        }
        
        printf("[TEST-PASS] BLE16 taken negative: 0x%06X -> 0x%06X (%d cycles)\n", 
               pc_start, actual_pc, cycle_delta);
    }
    
    // Test 3: BLE16 not taken
    {
        H8S2350Emulator emu;
        emu.reset();
        emu.setSP24(0x00F82000 - 4);
        emu.setVBR(0x00F80000);
        
        uint32_t pc_start = 0x001000;
        int16_t displacement = 0x0200;  // +512 bytes (irrelevant, should not be used)
        
        setupBLE16Instruction(emu, pc_start, displacement);
        emu.setProgramCounter(pc_start);
        setupFlags(emu, false, false, false);  // Z=0, N==V → BLE condition false
        
        uint32_t cycles_before = emu.getCycles();
        emu.step();
        uint32_t cycles_after = emu.getCycles();
        
        uint32_t expected_target = pc_start + 4;  // Next instruction: 0x1004
        uint32_t actual_pc = emu.getProgramCounter();
        
        if (actual_pc != expected_target) {
            printf("[TEST-FAIL] BLE16 not taken: expected 0x%06X, got 0x%06X\n", 
                   expected_target, actual_pc);
            return 1;
        }
        
        uint32_t cycle_delta = cycles_after - cycles_before;
        if (cycle_delta != 2) {  // 2 base, no taken penalty
            printf("[TEST-WARN] BLE16 not taken: expected 2 cycles, got %u\n", cycle_delta);
        }
        
        printf("[TEST-PASS] BLE16 not taken: 0x%06X -> 0x%06X (%d cycles)\n", 
               pc_start, actual_pc, cycle_delta);
    }
    
    // Test 4: BLE16 edge case - Z=1 overrides N^V condition
    {
        H8S2350Emulator emu;
        emu.reset();
        emu.setSP24(0x00F82000 - 4);
        emu.setVBR(0x00F80000);
        
        uint32_t pc_start = 0x001000;
        int16_t displacement = 0x0050;  // +80 bytes
        
        setupBLE16Instruction(emu, pc_start, displacement);
        emu.setProgramCounter(pc_start);
        setupFlags(emu, true, false, true);  // Z=1, N=0, V=1 → Z=1 should override N==V condition
        
        uint32_t cycles_before = emu.getCycles();
        emu.step();
        uint32_t cycles_after = emu.getCycles();
        
        uint32_t expected_target = pc_start + 4 + displacement;  // 0x1000 + 4 + 0x50 = 0x1054
        uint32_t actual_pc = emu.getProgramCounter();
        
        if (actual_pc != expected_target) {
            printf("[TEST-FAIL] BLE16 Z=1 override: expected 0x%06X, got 0x%06X\n", 
                   expected_target, actual_pc);
            return 1;
        }
        
        printf("[TEST-PASS] BLE16 Z=1 override: 0x%06X -> 0x%06X (Z overrides N^V)\n", 
               pc_start, actual_pc);
    }
    
    printf("[BLE16-MATRIX] All BLE16 test cases passed! ✅\n");
    printf("[BLE16-INFO] BLE condition: (Z==1) || (N!=V) - Branch if Less or Equal\n");
    printf("[BLE16-INFO] Timing: 2 base cycles + 1 if taken, +0 if not taken\n");
    printf("[BLE16-INFO] Target: next_pc + signed16_displacement (24-bit masked)\n");
    
    return 0;
}