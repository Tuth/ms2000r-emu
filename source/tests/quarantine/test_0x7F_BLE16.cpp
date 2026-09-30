// Test for 0x7F BLE16 branch instruction (previously commented out due to conflicts)
#include "core/h8s2350_emulator.h"
#include "core/h8s2350_contracts.h"
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace MS2000;

int main() {
    H8S2350Emulator emu;
    
    emu.reset();
    emu.setSP24(0x00F82000 - 4);
    emu.setVBR(0x00F80000);
    
    // Test 0x7F BLE16 extended branch instruction: 0x00 0x7F displacement
    uint32_t pc_start = 0x001000;
    int16_t displacement = 0x0100;  // Jump forward 256 bytes
    
    // Install extended BLE16 instruction: 0x00 0x7F disp_hi disp_lo
    emu.writeByte(pc_start, 0x00);     // Extended prefix
    emu.writeByte(pc_start + 1, 0x7F); // BLE16 opcode
    emu.writeByte(pc_start + 2, (displacement >> 8) & 0xFF); // displacement high
    emu.writeByte(pc_start + 3, displacement & 0xFF);        // displacement low
    
    emu.setProgramCounter(pc_start);
    
    // Test case 1: BLE condition true (Z=1) - should branch
    auto& flags = emu.getFlags();
    flags.zero = 1;      // Z=1
    flags.negative = 0;  // N=0 
    flags.overflow = 0;  // V=0
    
    uint32_t cycles_before = emu.getCycles();
    
    try {
        emu.step();
    } catch (...) {
        printf("[TEST-FAIL] 0x7F BLE16 instruction execution failed\n");
        return 1;
    }
    
    uint32_t cycles_after = emu.getCycles();
    
    uint32_t expected_target = pc_start + 4 + displacement;  // PC after instruction + displacement
    uint32_t actual_pc = emu.getProgramCounter();
    
    if (actual_pc != expected_target) {
        printf("[TEST-FAIL] 0x7F BLE16 branch target: expected 0x%06X, got 0x%06X\n", 
               expected_target, actual_pc);
        return 1;
    }
    
    // Verify cycles were consumed
    uint32_t cycle_delta = cycles_after - cycles_before;
    if (cycle_delta == 0) {
        printf("[TEST-WARN] 0x7F BLE16 consumed 0 cycles (timing not implemented?)\n");
    } else {
        printf("[TEST-INFO] 0x7F BLE16 consumed %u cycles\n", cycle_delta);
    }
    
    printf("[TEST-PASS] 0x7F BLE16 extended branch instruction working correctly\n");
    printf("[TEST-INFO] Branch taken from 0x%06X to 0x%06X (displacement=%d)\n", 
           pc_start, actual_pc, displacement);
    
    return 0;
}