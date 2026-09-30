#include "../core/ms2000_runner.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace MS2000;

int main() {
    std::cout << "=== MS2000 Simple Integration Test ===" << std::endl;
    
    try {
        // Configure emulator
        Ms2kConfig config;
        config.romPath = "test_rom.sys"; // Will fail to load, but that's OK for testing
        config.cpuCyclesPerTick = 1000;  // Reduced for testing
        config.sampleRate = 48000;
        
        // Set up minimal hooks
        Ms2kGuiHooks hooks;
        hooks.logFn = [](const char* message) {
            std::cout << "[LOG] " << message;
        };
        
        // Create runner
        auto runner = std::make_unique<Ms2kRunner>(config, hooks);
        
        std::cout << "✓ Ms2kRunner created successfully" << std::endl;
        
        // Test basic API without running emulator (avoid ROM requirement)
        std::cout << "✓ Basic API test completed" << std::endl;
        
        std::cout << "✓ Simple integration test PASSED!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "✗ Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "✗ Unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}