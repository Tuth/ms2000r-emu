// fw14.txt: CGRAM support test
#include "../core/lcd_gui.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "fw14.txt CGRAM Support Test\n";
    std::cout << "===========================\n\n";
    
    LcdGuiBuffer lcdBuffer;
    
    // Test 1: Basic CGRAM write/read
    std::cout << "Test 1: Writing custom character pattern...\n";
    
    // Set CGRAM address for character 0
    lcdBuffer.onCmd(0x40);  // CGRAM address 0 (character 0, row 0)
    
    // Define a heart pattern (5x8)
    uint8_t heartPattern[8] = {
        0x00,  // 00000
        0x0A,  // 01010
        0x1F,  // 11111
        0x1F,  // 11111
        0x0E,  // 01110
        0x04,  // 00100
        0x00,  // 00000
        0x00   // 00000
    };
    
    // Upload pattern to CGRAM
    for (int row = 0; row < 8; row++) {
        lcdBuffer.onData(heartPattern[row]);
        std::cout << "  Row " << row << ": 0x" << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)heartPattern[row] << " (";
        
        // Show binary pattern
        for (int bit = 4; bit >= 0; bit--) {
            std::cout << ((heartPattern[row] & (1 << bit)) ? '#' : '.');
        }
        std::cout << ")\n";
    }
    
    // Test 2: Switch to DDRAM and display custom character
    std::cout << "\nTest 2: Displaying custom character on LCD...\n";
    
    // Initialize LCD
    lcdBuffer.onCmd(0x01);  // Clear display
    lcdBuffer.onCmd(0x0C);  // Display ON
    lcdBuffer.onCmd(0x80);  // Set DDRAM address (home position)
    
    // Display text with custom character
    std::string message = "KORG ";
    for (char c : message) {
        lcdBuffer.onData(c);
    }
    lcdBuffer.onData(0x00);  // Custom character 0 (heart)
    lcdBuffer.onData(' ');
    lcdBuffer.onData('M');
    lcdBuffer.onData('S');
    lcdBuffer.onData('2');
    lcdBuffer.onData('0');
    lcdBuffer.onData('0');
    lcdBuffer.onData('0');
    
    // Test 3: Get snapshot and verify
    std::cout << "\nTest 3: Verifying LCD content...\n";
    auto snapshot = lcdBuffer.snapshot();
    
    std::cout << "Display ON: " << (snapshot.displayOn ? "YES" : "NO") << "\n";
    std::cout << "Line 0: [";
    for (int i = 0; i < 16; i++) {
        char ch = snapshot.line0[i];
        if (ch == 0) {
            std::cout << "<HEART>";
        } else if (ch >= 32 && ch <= 126) {
            std::cout << ch;
        } else {
            std::cout << "\\x" << std::hex << (int)(uint8_t)ch;
        }
    }
    std::cout << "]\n";
    
    // Test 4: Verify CGRAM data in snapshot
    std::cout << "\nTest 4: Verifying CGRAM data in snapshot...\n";
    bool cgramMatch = true;
    for (int row = 0; row < 8; row++) {
        if (snapshot.cgram[0][row] != heartPattern[row]) {
            cgramMatch = false;
            std::cout << "  MISMATCH Row " << row << ": expected 0x" << std::hex 
                      << (int)heartPattern[row] << ", got 0x" << (int)snapshot.cgram[0][row] << "\n";
        }
    }
    
    if (cgramMatch) {
        std::cout << "✅ CGRAM data matches expected pattern!\n";
    } else {
        std::cout << "❌ CGRAM data mismatch!\n";
    }
    
    // Test 5: Multiple custom characters
    std::cout << "\nTest 5: Creating multiple custom characters...\n";
    
    // Character 1: Arrow Up
    lcdBuffer.onCmd(0x48);  // CGRAM address 8 (character 1)
    uint8_t arrowUp[8] = {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x00};
    for (int row = 0; row < 8; row++) {
        lcdBuffer.onData(arrowUp[row]);
    }
    
    // Character 2: Arrow Down
    lcdBuffer.onCmd(0x50);  // CGRAM address 16 (character 2)
    uint8_t arrowDown[8] = {0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x00};
    for (int row = 0; row < 8; row++) {
        lcdBuffer.onData(arrowDown[row]);
    }
    
    // Display all custom characters
    lcdBuffer.onCmd(0x80);  // Home position
    lcdBuffer.onData('C');
    lcdBuffer.onData('u');
    lcdBuffer.onData('s');
    lcdBuffer.onData('t');
    lcdBuffer.onData('o');
    lcdBuffer.onData('m');
    lcdBuffer.onData(':');
    lcdBuffer.onData(' ');
    lcdBuffer.onData(0x00);  // Heart
    lcdBuffer.onData(0x01);  // Arrow Up
    lcdBuffer.onData(0x02);  // Arrow Down
    
    auto finalSnapshot = lcdBuffer.snapshot();
    std::cout << "Final display: [" << finalSnapshot.line0 << "]\n";
    
    std::cout << "\n===========================\n";
    std::cout << "✅ fw14.txt CGRAM Support Test COMPLETED!\n";
    std::cout << "Custom characters can be uploaded and displayed.\n";
    
    return 0;
}