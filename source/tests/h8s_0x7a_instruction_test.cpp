// i12.txt: 0x7A Instruction Group Regression Tests
// Ensures proper decoding and execution of H8S/2350 Advanced Mode 24-bit instructions

#include <iostream>
#include <vector>
#include <cassert>
#include <cstdint>

// Include H8S emulator headers
#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_instructions.h"

using namespace MS2000;

class H8S0x7AInstructionTest {
private:
    H8S2350Emulator emulator;
    
    void setupTestEnvironment() {
        emulator.reset();
        // Set up Mode 4 Advanced (24-bit PC, 32-bit vectors)
        emulator.setMode(H8S2350Mode::MODE_4);
    }
    
    void writeTestBytes(uint32_t address, const std::vector<uint8_t>& bytes) {
        for (size_t i = 0; i < bytes.size(); i++) {
            emulator.writeByte(address + i, bytes[i]);
        }
    }

public:
    void runAllTests() {
        std::cout << "[0x7A-TEST] Starting H8S/2350 0x7A instruction group regression tests...\n";
        
        test_0x7A_MOV_B_to_FFFC();
        test_0x7A_JSR_to_0810();
        test_0x7A_MOV_L_from_abs24();
        
        std::cout << "[0x7A-TEST] ✅ All 0x7A instruction group tests PASSED\n";
    }
    
    // i12.txt Test 1: 7A 07 00 FF FC (MOV.B → @FFFC)
    void test_0x7A_MOV_B_to_FFFC() {
        std::cout << "[0x7A-TEST] Testing MOV.B R7(=0x00) -> @0x00FFFC...\n";
        
        setupTestEnvironment();
        
        // Set up test: R7 = 0x00, write to MSTPCR (0x00FFFC)
        emulator.setRegisterByte(7, 0x00);  // R7 = 0x00
        emulator.setPC(0x810);  // Start at firmware entry point
        
        // Write instruction: 7A 07 00 FF FC (MOV.B R7 → @0x00FFFC)
        std::vector<uint8_t> instruction = {0x7A, 0x07, 0x00, 0xFF, 0xFC};
        writeTestBytes(0x810, instruction);
        
        // Execute instruction
        emulator.step();
        
        // Verify: MSTPCR register should be written with 0x00
        uint8_t mstpcr_value = emulator.readByte(0x00FFFC);
        assert(mstpcr_value == 0x00);
        
        // Verify PC advanced correctly (5-byte instruction)
        uint32_t pc = emulator.getPC();
        assert(pc == 0x815);
        
        std::cout << "[0x7A-TEST] ✅ MOV.B R7 -> @FFFC test passed\n";
    }
    
    // i12.txt Test 2: 7A 80 00 08 10 (JSR @0x000810)
    void test_0x7A_JSR_to_0810() {
        std::cout << "[0x7A-TEST] Testing JSR @0x000810...\n";
        
        setupTestEnvironment();
        
        emulator.setPC(0x1000);  // Start at different location
        uint32_t original_sp = emulator.getSP();
        
        // Write instruction: 7A 80 00 08 10 (JSR @0x000810)
        std::vector<uint8_t> instruction = {0x7A, 0x80, 0x00, 0x08, 0x10};
        writeTestBytes(0x1000, instruction);
        
        // Execute instruction
        emulator.step();
        
        // Verify: PC should jump to 0x000810
        uint32_t pc = emulator.getPC();
        assert(pc == 0x000810);
        
        // Verify: Return address (0x1005) should be pushed to stack
        uint32_t new_sp = emulator.getSP();
        assert(new_sp == original_sp - 4);  // 32-bit address pushed
        
        uint32_t return_addr = emulator.readLong(new_sp);
        assert(return_addr == 0x1005);  // Next instruction after JSR
        
        std::cout << "[0x7A-TEST] ✅ JSR @0x000810 test passed\n";
    }
    
    // i12.txt Test 3: 7A 64 00 10 00 (MOV.L @abs24 → ER4)
    void test_0x7A_MOV_L_from_abs24() {
        std::cout << "[0x7A-TEST] Testing MOV.L @abs24 -> ER4...\n";
        
        setupTestEnvironment();
        
        emulator.setPC(0x2000);
        
        // Set up test data at 0x001000
        emulator.writeLong(0x001000, 0x12345678);
        
        // Write instruction: 7A 64 00 10 00 (MOV.L @0x001000 → ER4)
        std::vector<uint8_t> instruction = {0x7A, 0x64, 0x00, 0x10, 0x00};
        writeTestBytes(0x2000, instruction);
        
        // Execute instruction
        emulator.step();
        
        // Verify: ER4 should contain the loaded value
        uint32_t er4_value = emulator.getRegisterLong(4);
        assert(er4_value == 0x12345678);
        
        // Verify PC advanced correctly
        uint32_t pc = emulator.getPC();
        assert(pc == 0x2005);
        
        std::cout << "[0x7A-TEST] ✅ MOV.L @abs24 -> ER4 test passed\n";
    }
};

// Main test runner function
void run_h8s_0x7a_instruction_tests() {
    try {
        H8S0x7AInstructionTest tester;
        tester.runAllTests();
    } catch (const std::exception& e) {
        std::cerr << "[0x7A-TEST] ❌ Test failed with exception: " << e.what() << std::endl;
        throw;
    }
}

// Stand-alone main for direct testing
#ifdef H8S_0x7A_TEST_STANDALONE
int main() {
    try {
        run_h8s_0x7a_instruction_tests();
        std::cout << "[0x7A-TEST] All regression tests completed successfully!\n";
        return 0;
    } catch (...) {
        std::cout << "[0x7A-TEST] Regression tests FAILED!\n";
        return 1;
    }
}
#endif