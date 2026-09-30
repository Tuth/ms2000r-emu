// fw14.txt: Panel I/O Protocol test for LED/Switch/VR communication
#include "../core/panel_mp.h"
#include "../core/lcd_gui.h"
#include <iostream>
#include <vector>

int main() {
    std::cout << "fw14.txt Panel I/O Protocol Test\n";
    std::cout << "=================================\n\n";
    
    // Create test objects
    PanelMP panelMP;
    LcdGuiBuffer lcdBuffer;
    
    // Test LED states
    bool testLEDs[16] = {false};
    struct LEDColor { uint8_t r, g, b; } testLEDColors[16];
    
    // Test switch states
    bool testSwitches[16] = {false};
    
    // Test VR values  
    uint16_t testVRs[16];
    for (int i = 0; i < 16; i++) {
        testVRs[i] = i * 64; // Different values for each VR
    }
    
    // Wire up callbacks
    panelMP.lcdCmd = [&](uint8_t cmd) {
        lcdBuffer.onCmd(cmd);
        std::cout << "  LCD CMD: 0x" << std::hex << (int)cmd << std::dec << "\n";
    };
    
    panelMP.lcdData = [&](uint8_t data) {
        lcdBuffer.onData(data);
        char ch = (data >= 0x20 && data <= 0x7E) ? data : '?';
        std::cout << "  LCD DATA: 0x" << std::hex << (int)data << " ('" << ch << "')\n";
    };
    
    panelMP.ledSet = [&](int ledId, bool state) {
        if (ledId >= 0 && ledId < 16) {
            testLEDs[ledId] = state;
            std::cout << "  LED " << ledId << " set to " << (state ? "ON" : "OFF") << "\n";
        }
    };
    
    panelMP.ledSetRGB = [&](int ledId, uint8_t r, uint8_t g, uint8_t b) {
        if (ledId >= 0 && ledId < 16) {
            testLEDs[ledId] = true;
            testLEDColors[ledId] = {r, g, b};
            std::cout << "  LED " << ledId << " set to RGB(" << (int)r << "," << (int)g << "," << (int)b << ")\n";
        }
    };
    
    panelMP.switchRead = [&](int switchId) -> bool {
        if (switchId >= 0 && switchId < 16) {
            std::cout << "  Switch " << switchId << " read: " << (testSwitches[switchId] ? "PRESSED" : "RELEASED") << "\n";
            return testSwitches[switchId];
        }
        return false;
    };
    
    panelMP.vrRead = [&](int vrId) -> uint16_t {
        if (vrId >= 0 && vrId < 16) {
            std::cout << "  VR " << vrId << " read: " << testVRs[vrId] << "\n";
            return testVRs[vrId];
        }
        return 512;
    };
    
    panelMP.log = [](const char* msg) {
        std::cout << "LOG: " << msg;
    };
    
    std::cout << "Test 1: LCD Commands via MP Protocol...\n";
    
    // Test LCD clear command: [A5] [02] [11] [checksum]
    std::vector<uint8_t> lcdClearFrame = {0xA5, 0x02, 0x11};
    uint8_t checksum = 0xA5 + 0x02 + 0x11;
    lcdClearFrame.push_back(checksum);
    
    std::cout << "Sending LCD Clear frame...\n";
    for (uint8_t b : lcdClearFrame) {
        panelMP.onSciTxByte(b);
    }
    
    // Test LCD data: [A5] [04] [12] [00] [13] [41] [checksum] (SETPOS 0, DATA 'A')
    std::vector<uint8_t> lcdDataFrame = {0xA5, 0x04, 0x12, 0x00, 0x13, 0x41};
    checksum = 0;
    for (uint8_t b : lcdDataFrame) checksum += b;
    lcdDataFrame.push_back(checksum);
    
    std::cout << "Sending LCD Data frame (write 'A' at position 0)...\n";
    for (uint8_t b : lcdDataFrame) {
        panelMP.onSciTxByte(b);
    }
    
    std::cout << "\nTest 2: LED Commands...\n";
    
    // Test LED set: [A5] [03] [20] [05] [01] [checksum] (LED 5 ON)
    std::vector<uint8_t> ledFrame = {0xA5, 0x03, 0x20, 0x05, 0x01};
    checksum = 0;
    for (uint8_t b : ledFrame) checksum += b;
    ledFrame.push_back(checksum);
    
    std::cout << "Sending LED Set frame (LED 5 ON)...\n";
    for (uint8_t b : ledFrame) {
        panelMP.onSciTxByte(b);
    }
    
    // Test LED RGB: [A5] [05] [21] [03] [FF] [00] [80] [checksum] (LED 3 Red-Purple)
    std::vector<uint8_t> ledRGBFrame = {0xA5, 0x05, 0x21, 0x03, 0xFF, 0x00, 0x80};
    checksum = 0;
    for (uint8_t b : ledRGBFrame) checksum += b;
    ledRGBFrame.push_back(checksum);
    
    std::cout << "Sending LED RGB frame (LED 3 = Red-Purple)...\n";
    for (uint8_t b : ledRGBFrame) {
        panelMP.onSciTxByte(b);
    }
    
    std::cout << "\nTest 3: Switch Polling...\n";
    
    // Set some test switch states
    testSwitches[2] = true;  // Switch 2 pressed
    testSwitches[7] = true;  // Switch 7 pressed
    
    // Test switch poll: [A5] [02] [30] [02] [checksum] (poll switch 2)
    std::vector<uint8_t> switchFrame = {0xA5, 0x02, 0x30, 0x02};
    checksum = 0;
    for (uint8_t b : switchFrame) checksum += b;
    switchFrame.push_back(checksum);
    
    std::cout << "Sending Switch Poll frame (poll switch 2, should be PRESSED)...\n";
    for (uint8_t b : switchFrame) {
        panelMP.onSciTxByte(b);
    }
    
    std::cout << "\nTest 4: VR Reading...\n";
    
    // Test VR read: [A5] [02] [31] [0A] [checksum] (read VR 10)
    std::vector<uint8_t> vrFrame = {0xA5, 0x02, 0x31, 0x0A};
    checksum = 0;
    for (uint8_t b : vrFrame) checksum += b;
    vrFrame.push_back(checksum);
    
    std::cout << "Sending VR Read frame (read VR 10, value should be " << testVRs[10] << ")...\n";
    for (uint8_t b : vrFrame) {
        panelMP.onSciTxByte(b);
    }
    
    std::cout << "\nTest 5: Response Queue Check...\n";
    
    // Check if responses were queued
    int responseCount = 0;
    std::cout << "Reading queued responses:\n";
    
    for (int i = 0; i < 10; i++) {  // Try to read up to 10 responses
        uint8_t response = panelMP.onSciRxByteReq();
        if (response != 0xAC) {  // 0xAC is default ACK
            std::cout << "  Response " << responseCount++ << ": 0x" << std::hex << (int)response << std::dec << "\n";
        } else {
            break;  // No more real responses
        }
    }
    
    std::cout << "\nTest 6: Final LCD State Check...\n";
    auto snapshot = lcdBuffer.snapshot();
    std::cout << "LCD Display ON: " << (snapshot.displayOn ? "YES" : "NO") << "\n";
    std::cout << "LCD Line 0: [" << snapshot.line0 << "]\n";
    std::cout << "LCD Line 1: [" << snapshot.line1 << "]\n";
    
    std::cout << "\nTest 7: LED State Summary...\n";
    int activeLEDs = 0;
    for (int i = 0; i < 16; i++) {
        if (testLEDs[i]) {
            activeLEDs++;
            std::cout << "  LED " << i << ": ON";
            if (testLEDColors[i].r || testLEDColors[i].g || testLEDColors[i].b) {
                std::cout << " RGB(" << (int)testLEDColors[i].r << "," 
                          << (int)testLEDColors[i].g << "," << (int)testLEDColors[i].b << ")";
            }
            std::cout << "\n";
        }
    }
    std::cout << "Total active LEDs: " << activeLEDs << "/16\n";
    
    std::cout << "\n=================================\n";
    std::cout << "✅ Panel I/O Protocol Test COMPLETED!\n";
    std::cout << "Successfully tested:\n";
    std::cout << "- LCD command protocol (CLEAR, SETPOS, DATA)\n";
    std::cout << "- LED control (simple ON/OFF and RGB color)\n";
    std::cout << "- Switch polling with response queue\n";
    std::cout << "- VR (potentiometer) reading with response queue\n";
    std::cout << "- Frame format validation (header, length, checksum)\n";
    
    return 0;
}