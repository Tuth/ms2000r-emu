// fw12.txt: LCD GUI integration test
#include "../core/lcd_gui.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    std::cout << "fw12.txt LCD GUI Buffer Test\n" << std::endl;
    
    LcdGuiBuffer gui;
    
    // Test LCD initialization sequence
    std::cout << "Testing LCD initialization sequence...\n" << std::endl;
    gui.onCmd(0x38);  // Function Set (8-bit, 2-line, 5x8 dots)
    gui.onCmd(0x0C);  // Display ON, cursor OFF
    gui.onCmd(0x01);  // Clear Display
    gui.onCmd(0x06);  // Entry Mode Set (increment, no shift)
    gui.onCmd(0x80);  // Set DDRAM Address (line 1, pos 0)
    
    // Test writing KORG MS2000 text
    std::cout << "Writing 'KORG MS2000' text...\n" << std::endl;
    std::string text = "KORG MS2000";
    for (char c : text) {
        gui.onData(static_cast<uint8_t>(c));
    }
    
    // Get snapshot and display
    auto snapshot = gui.snapshot();
    
    std::cout << "=== LCD Display State ===" << std::endl;
    std::cout << "Display ON: " << (snapshot.displayOn ? "YES" : "NO") << std::endl;
    std::cout << "Cursor ON:  " << (snapshot.cursorOn ? "YES" : "NO") << std::endl;
    std::cout << "Blink ON:   " << (snapshot.blinkOn ? "YES" : "NO") << std::endl;
    std::cout << "Cursor Position: Line=" << snapshot.curLine << ", Col=" << snapshot.curCol << std::endl;
    std::cout << std::endl;
    
    std::cout << "=== LCD Content ===" << std::endl;
    std::cout << "[" << snapshot.line0 << "]" << std::endl;
    std::cout << "[" << snapshot.line1 << "]" << std::endl;
    std::cout << std::endl;
    
    // Test cursor to line 2
    std::cout << "Moving to line 2 and writing more text...\n" << std::endl;
    gui.onCmd(0xC0);  // Set DDRAM Address (line 2, pos 0)
    std::string text2 = "    SYNTH";
    for (char c : text2) {
        gui.onData(static_cast<uint8_t>(c));
    }
    
    // Final snapshot
    snapshot = gui.snapshot();
    std::cout << "=== Final LCD Content ===" << std::endl;
    std::cout << "[" << snapshot.line0 << "]" << std::endl;
    std::cout << "[" << snapshot.line1 << "]" << std::endl;
    
    if (std::string(snapshot.line0) == "KORG MS2000     " && 
        std::string(snapshot.line1).substr(0, 9) == "    SYNTH") {
        std::cout << "\n✅ fw12.txt LCD GUI Buffer Test: SUCCESS!" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ fw12.txt LCD GUI Buffer Test: FAILED!" << std::endl;
        return 1;
    }
}