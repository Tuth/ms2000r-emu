#include <iostream>
#include <thread>
#include <chrono>
#include "../core/h8s2350_emulator.h"
#include "../core/real_lcd_display.h"
#include "../core/h8s_lcd_adapter.h"

using namespace MS2000;

int main() {
    std::cout << "📺 LCD Test - Verifying LCD Display Functionality" << std::endl;
    
    // Create H8S emulator and LCD display
    H8S2350Emulator h8s;
    h8s.setDebugMode(true);  // Enable debug to see TRAPA vectors
    RealLCDDisplay lcd;
    
    // Initialize H8S
    if (!h8s.initialize()) {
        std::cout << "❌ Failed to initialize H8S emulator!" << std::endl;
        return 1;
    }
    
    // Create LCD adapter
    H8SLCDAdapter lcdAdapter(h8s, lcd);
    
    // Set test text directly on LCD
    lcd.setText(0, "LCD Test Line 1");
    lcd.setText(1, "LCD Test Line 2");
    
    std::cout << "📺 LCD initialized with test text:" << std::endl;
    std::cout << "Line 0: '" << lcd.getText(0) << "'" << std::endl;
    std::cout << "Line 1: '" << lcd.getText(1) << "'" << std::endl;
    
    // Load firmware to see if it writes to LCD
    if (h8s.loadFirmwareFromFile("flash.bin")) {
        std::cout << "✅ Firmware loaded successfully" << std::endl;
        h8s.reset();
        
        // Run emulator for a few seconds to see if firmware writes to LCD
        std::cout << "🔄 Running emulator for 5 seconds..." << std::endl;
        
        auto startTime = std::chrono::steady_clock::now();
        uint32_t cycleCount = 0;
        
        while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(5)) {
            // Execute some cycles
            for (int i = 0; i < 1000; i++) {
                h8s.step();
                cycleCount++;
            }
            
            // Process LCD commands
            lcdAdapter.processLCDCommands();
            
            // Check if LCD content changed
            if (lcd.hasChanged()) {
                std::cout << "📺 LCD content changed!" << std::endl;
                std::cout << "Line 0: '" << lcd.getText(0) << "'" << std::endl;
                std::cout << "Line 1: '" << lcd.getText(1) << "'" << std::endl;
                lcd.clearChanged();
            }
            
            // Small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "✅ Emulator ran for " << cycleCount << " cycles" << std::endl;
        std::cout << "📺 Final LCD content:" << std::endl;
        std::cout << "Line 0: '" << lcd.getText(0) << "'" << std::endl;
        std::cout << "Line 1: '" << lcd.getText(1) << "'" << std::endl;
        
    } else {
        std::cout << "❌ Failed to load firmware!" << std::endl;
    }
    
    std::cout << "📺 LCD test completed!" << std::endl;
    return 0;
}
