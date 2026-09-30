#include "core/h8s2350_emulator.h"
#include <iostream>

using namespace MS2000;

int main() {
    std::cout << "=== TRAPA Debug Test ===" << std::endl;
    
    std::cout << "1. Creating emulator..." << std::endl;
    H8S2350Emulator emulator;
    emulator.setDebugMode(true);  // Enable debug mode to see TRAPA vectors
    std::cout << "   ✅ Emulator created with debug enabled" << std::endl;
    
    std::cout << "2. Initializing emulator..." << std::endl;
    if (!emulator.initialize()) {
        std::cout << "   ❌ Initialization failed" << std::endl;
        return 1;
    }
    std::cout << "   ✅ Emulator initialized" << std::endl;
    
    std::cout << "3. Loading firmware..." << std::endl;
    if (!emulator.loadFirmwareFromFile("flash.bin")) {
        std::cout << "   ❌ Firmware loading failed" << std::endl;
        return 1;
    }
    std::cout << "   ✅ Firmware loaded" << std::endl;
    
    std::cout << "4. Resetting emulator..." << std::endl;
    emulator.reset();
    std::cout << "   ✅ Emulator reset" << std::endl;
    
    std::cout << "5. Running firmware until TRAPA vectors are initialized..." << std::endl;
    uint32_t cycles_before = emulator.getExecutedCycles();
    
    // Run firmware long enough to see if it hits valid TRAPA calls
    for (int i = 0; i < 50000 && !emulator.isHalted(); i++) {
        emulator.step();
        if (i % 10000 == 0) {
            std::cout << "   Executed " << (i + 1) << " steps, PC: 0x" << std::hex << emulator.getProgramCounter() << std::dec << std::endl;
        }
    }
    
    uint32_t cycles_after = emulator.getExecutedCycles();
    std::cout << "   Executed " << (cycles_after - cycles_before) << " CPU cycles" << std::endl;
    std::cout << "   Final PC: 0x" << std::hex << emulator.getProgramCounter() << std::dec << std::endl;
    
    std::cout << "=== TRAPA debug test completed ===" << std::endl;
    return 0;
}
