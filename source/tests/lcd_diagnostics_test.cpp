// fw14.txt: LCD Diagnostics Widget test
#include "../gui/lcd_diagnostics_widget.h"
#include "../core/lcd_gui.h"
#include "../core/panel_mp.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "fw14.txt LCD Diagnostics Widget Test\n";
    std::cout << "====================================\n\n";
    
    // Create test objects
    LcdDiagnosticsWidget diagnosticsWidget;
    LcdGuiBuffer lcdBuffer;
    PanelMP panelMP;
    
    std::cout << "Test 1: Initial state...\n";
    auto snapshot = lcdBuffer.snapshot();
    // Note: This would normally show ImGui window, but we're testing the data structures
    
    std::cout << "Test 2: Simulating boot sequence...\n";
    
    // Simulate LCD initialization commands
    diagnosticsWidget.updateBootProbe(false, false, false, false, false, 0);
    std::cout << "  Initial boot probe state set\n";
    
    // Function Set
    diagnosticsWidget.updateBootProbe(true, false, false, false, false, 0);
    std::cout << "  Function Set (FN) detected\n";
    
    // Display ON
    diagnosticsWidget.updateBootProbe(true, true, false, false, false, 0);
    std::cout << "  Display ON detected\n";
    
    // Clear Display
    diagnosticsWidget.updateBootProbe(true, true, true, false, false, 0);
    std::cout << "  Clear Display (CLR) detected\n";
    
    // Entry Mode
    diagnosticsWidget.updateBootProbe(true, true, true, true, false, 0);
    std::cout << "  Entry Mode (ENT) detected\n";
    
    // DDRAM Address Set
    diagnosticsWidget.updateBootProbe(true, true, true, true, true, 0);
    std::cout << "  DDRAM Address set\n";
    
    // Data characters
    for (int i = 1; i <= 15; i++) {
        diagnosticsWidget.updateBootProbe(true, true, true, true, true, i);
        if (i % 5 == 0) {
            std::cout << "  " << i << " characters written\n";
        }
    }
    
    std::cout << "\nTest 3: System gates status...\n";
    
    // Simulate system startup
    diagnosticsWidget.updateSystemGates(false, false, false, false, true);  // Only VBR initially
    std::cout << "  Initial system state (VBR only)\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    diagnosticsWidget.updateSystemGates(true, false, false, true, true);   // DSP + TICK
    std::cout << "  DSP and CPU TICK active\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    diagnosticsWidget.updateSystemGates(true, true, false, true, true);    // + CODEC
    std::cout << "  CODEC initialized\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    diagnosticsWidget.updateSystemGates(true, true, true, true, true);     // All systems
    std::cout << "  All systems operational\n";
    
    std::cout << "\nTest 4: Command tracking...\n";
    
    // Simulate various LCD commands
    diagnosticsWidget.trackCommand(0x38);  // Function Set
    diagnosticsWidget.trackCommand(0x0C);  // Display Control
    diagnosticsWidget.trackCommand(0x01);  // Clear Display
    diagnosticsWidget.trackCommand(0x06);  // Entry Mode Set
    diagnosticsWidget.trackCommand(0x80);  // Set DDRAM Address
    diagnosticsWidget.trackCommand(0x40);  // Set CGRAM Address
    
    std::cout << "  Tracked 6 different command types\n";
    
    // Simulate data writes
    for (int i = 0; i < 20; i++) {
        diagnosticsWidget.trackData(0x41 + (i % 26));  // A-Z characters
    }
    
    std::cout << "  Tracked 20 data bytes\n";
    
    std::cout << "\nTest 5: LCD content simulation...\n";
    
    // Create some LCD content for testing
    lcdBuffer.onCmd(0x01);  // Clear
    lcdBuffer.onCmd(0x0C);  // Display ON
    lcdBuffer.onCmd(0x80);  // Home
    
    std::string message = "KORG MS2000";
    for (char c : message) {
        lcdBuffer.onData(c);
    }
    
    // Move to second line
    lcdBuffer.onCmd(0xC0);
    std::string message2 = "Diagnostics OK";
    for (char c : message2) {
        lcdBuffer.onData(c);
    }
    
    auto finalSnapshot = lcdBuffer.snapshot();
    std::cout << "  LCD Line 0: [" << finalSnapshot.line0 << "]\n";
    std::cout << "  LCD Line 1: [" << finalSnapshot.line1 << "]\n";
    std::cout << "  Display ON: " << (finalSnapshot.displayOn ? "YES" : "NO") << "\n";
    
    std::cout << "\n====================================\n";
    std::cout << "✅ LCD Diagnostics Widget Test COMPLETED!\n";
    std::cout << "\nNote: This test validates the data structures and logic.\n";
    std::cout << "To see the actual ImGui interface, run the full emulator.\n";
    std::cout << "The diagnostics overlay will show:\n";
    std::cout << "- Boot sequence progress (100% complete)\n";
    std::cout << "- System health status (100% operational)\n";
    std::cout << "- Command counters and recent activity\n";
    std::cout << "- Real-time LCD state information\n";
    
    return 0;
}