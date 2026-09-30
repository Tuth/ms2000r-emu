// Sprint 1.2: Mini cycle smoke test - taken vs not-taken validation
// DoD: same code taken vs not-taken différence ≥1; TRAPA/RTE blocks cycle consistency

#include "h8s2350_emulator.h"
#include "h8s2350_contracts.h"
#include <iostream>
#include <cassert>

namespace MS2000 {

class CycleSmokeTest {
private:
    H8S2350Emulator m_cpu;

    void setupTestCPU() {
        m_cpu.reset();
        m_cpu.resetCycles();
        // Load minimal firmware ROM for testing
        m_cpu.loadROMFromFile("test_data/minimal_test.sys");
    }

public:
    // Test 1: Branch taken vs not-taken cycle difference
    bool testBranchCycleDifference() {
        std::cout << "[CYCLE-SMOKE] Testing branch taken vs not-taken..." << std::endl;
        
        setupTestCPU();
        
        // Test BEQ instruction with zero flag set (taken)
        m_cpu.getFlags().zero = true;
        uint64_t cycles_before_taken = m_cpu.getCycles();
        m_cpu.writeByte(0x1000, 0x1C); // BEQ
        m_cpu.writeByte(0x1001, 0x10); // displacement +16
        m_cpu.setProgramCounter(0x1000);
        m_cpu.step();
        uint64_t cycles_taken = m_cpu.getCycles() - cycles_before_taken;
        
        // Reset and test BEQ with zero flag clear (not taken)
        setupTestCPU();
        m_cpu.getFlags().zero = false;
        uint64_t cycles_before_not_taken = m_cpu.getCycles();
        m_cpu.writeByte(0x1000, 0x1C); // BEQ
        m_cpu.writeByte(0x1001, 0x10); // displacement +16
        m_cpu.setProgramCounter(0x1000);
        m_cpu.step();
        uint64_t cycles_not_taken = m_cpu.getCycles() - cycles_before_not_taken;
        
        std::cout << "  Taken cycles: " << cycles_taken << std::endl;
        std::cout << "  Not-taken cycles: " << cycles_not_taken << std::endl;
        std::cout << "  Difference: " << (cycles_taken - cycles_not_taken) << std::endl;
        
        // DoD: taken vs not-taken difference ≥1
        bool passed = (cycles_taken > cycles_not_taken) && 
                     ((cycles_taken - cycles_not_taken) >= 1);
        
        std::cout << "  Result: " << (passed ? "✅ PASS" : "❌ FAIL") << std::endl;
        return passed;
    }
    
    // Test 2: TRAPA/RTE cycle consistency
    bool testTrapaCycleConsistency() {
        std::cout << "[CYCLE-SMOKE] Testing TRAPA/RTE cycle consistency..." << std::endl;
        
        setupTestCPU();
        
        // Test TRAPA #0 cycles
        uint64_t cycles_before = m_cpu.getCycles();
        bool success = m_cpu.executeTrap(0);  // TRAPA #0
        uint64_t trapa_cycles = m_cpu.getCycles() - cycles_before;
        
        if (!success) {
            std::cout << "  TRAPA execution failed" << std::endl;
            return false;
        }
        
        // Test RTE cycles
        cycles_before = m_cpu.getCycles();
        success = m_cpu.executeRTE();
        uint64_t rte_cycles = m_cpu.getCycles() - cycles_before;
        
        if (!success) {
            std::cout << "  RTE execution failed" << std::endl;
            return false;
        }
        
        std::cout << "  TRAPA cycles: " << trapa_cycles << std::endl;
        std::cout << "  RTE cycles: " << rte_cycles << std::endl;
        
        // DoD: TRAPA/RTE blocks cycle consistency (should be > 0)
        bool passed = (trapa_cycles > 0) && (rte_cycles > 0);
        
        std::cout << "  Result: " << (passed ? "✅ PASS" : "❌ FAIL") << std::endl;
        return passed;
    }
    
    // Test 3: ALU immediate operations baseline
    bool testALUCycleBaseline() {
        std::cout << "[CYCLE-SMOKE] Testing ALU immediate cycle baseline..." << std::endl;
        
        setupTestCPU();
        
        // Test ADD #imm8,Rn cycles
        uint64_t cycles_before = m_cpu.getCycles();
        m_cpu.writeByte(0x1000, 0x20); // ADD #imm8,Rn
        m_cpu.writeByte(0x1001, 0x05); // Add 5 to R0
        m_cpu.setProgramCounter(0x1000);
        m_cpu.step();
        uint64_t add_cycles = m_cpu.getCycles() - cycles_before;
        
        // Test SUB #imm8,Rn cycles
        cycles_before = m_cpu.getCycles();
        m_cpu.writeByte(0x1002, 0x24); // SUB #imm8,Rn
        m_cpu.writeByte(0x1003, 0x03); // Subtract 3 from R0
        m_cpu.setProgramCounter(0x1002);
        m_cpu.step();
        uint64_t sub_cycles = m_cpu.getCycles() - cycles_before;
        
        std::cout << "  ADD cycles: " << add_cycles << std::endl;
        std::cout << "  SUB cycles: " << sub_cycles << std::endl;
        
        // DoD: ALU operations should have consistent base cycles
        bool passed = (add_cycles > 0) && (sub_cycles > 0) && (add_cycles == sub_cycles);
        
        std::cout << "  Result: " << (passed ? "✅ PASS" : "❌ FAIL") << std::endl;
        return passed;
    }
    
    // Run all smoke tests
    bool runAll() {
        std::cout << "=== P1 Cycle Model Smoke Test ===" << std::endl;
        
        int passed = 0;
        int total = 3;
        
        if (testBranchCycleDifference()) passed++;
        if (testTrapaCycleConsistency()) passed++;
        if (testALUCycleBaseline()) passed++;
        
        std::cout << std::endl;
        std::cout << "Results: " << passed << "/" << total << " tests passed" << std::endl;
        
        if (passed == total) {
            std::cout << "🎯 P1 Cycle Model: ALL SMOKE TESTS PASS!" << std::endl;
            return true;
        } else {
            std::cout << "❌ P1 Cycle Model: Some tests failed" << std::endl;
            return false;
        }
    }
};

} // namespace MS2000

// Standalone test runner
int main() {
    MS2000::CycleSmokeTest test;
    return test.runAll() ? 0 : 1;
}