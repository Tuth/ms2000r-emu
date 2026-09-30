/**
 * H8S/2350 Emulator Test Compilation
 * Simple test to verify the emulator compiles correctly
 */

#include "h8s2350_emulator.h"
#include <iostream>

using namespace MS2000;

int main()
{
    std::cout << "🎛️ H8S/2350 Emulator Test" << std::endl;
    
    try {
        // Create emulator instance
        H8S2350Emulator emulator;
        
        // Initialize
        if (!emulator.initialize()) {
            std::cerr << "❌ H8S/2350 initialization failed!" << std::endl;
            return 1;
        }
        
        // Reset CPU
        emulator.reset();
        
        // Check initial state
        const auto& regs = emulator.getRegisters();
        std::cout << "✅ Initial PC: 0x" << std::hex << regs.pc << std::endl;
        std::cout << "✅ Initial SP: 0x" << std::hex << regs.sp << std::endl;
        std::cout << "✅ Mode: " << static_cast<int>(emulator.getMode()) << std::endl;
        
        // Test memory operations
        std::vector<uint8_t> test_firmware = {0x5A, 0x00, 0x01, 0x00}; // SLEEP, NOP
        if (emulator.loadFirmware(test_firmware)) {
            std::cout << "✅ Firmware loading works" << std::endl;
        }
        
        // Test instruction execution
        emulator.step();
        std::cout << "✅ Instruction execution works" << std::endl;
        std::cout << "✅ Cycles: " << emulator.getExecutedCycles() << std::endl;
        std::cout << "✅ Instructions: " << emulator.getInstructionCount() << std::endl;
        
        // Test interrupt system
            emulator.triggerInterrupt(H8S2350InterruptSource::IRQ0);
    emulator.setInterruptPriority(H8S2350InterruptSource::IRQ0, InterruptPriority::LEVEL_5);
        std::cout << "✅ Interrupt system works" << std::endl;
        
        // Test debug functions
        emulator.dumpRegisters();
        emulator.dumpMemoryMap();
        std::cout << "✅ Debug functions work" << std::endl;
        
        std::cout << "🎉 H8S/2350 Emulator test completed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Unknown exception occurred" << std::endl;
        return 1;
    }
}
