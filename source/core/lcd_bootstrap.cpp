#include "lcd_bootstrap.h"
#include "real_lcd_display.h"
#include <chrono>
#include <thread>
#include <iostream>

namespace MS2000 {

void bootstrap_lcd_display_on(RealLCDDisplay& lcd, const LcdBootstrapConfig& cfg) {
    if (!cfg.enabled) return;

    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.delay_ms));

    if (cfg.log) {
        std::cerr << "[LCD-BOOT] Bootstrap diagnostics - BEFORE:" << std::endl;
        std::cerr << "  Display ON: " << (lcd.isDisplayOn() ? "YES" : "NO") << std::endl;
        std::cerr << "  Current text line 0: '" << lcd.getText(0) << "'" << std::endl;
        std::cerr << "  Current text line 1: '" << lcd.getText(1) << "'" << std::endl;
        std::cerr << "  DDRAM content: ";
        const auto& ddram = lcd.getDdRam();
        for (size_t i = 0; i < ddram.size() && i < 20; ++i) {
            char c = ddram[i];
            std::cerr << (c >= 32 && c <= 126 ? c : '.');
        }
        std::cerr << std::endl;
    }

    // Enhanced bootstrap sequence with full HD44780 initialization
    if (!lcd.isDisplayOn()) {
        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Display not ON - performing full bootstrap sequence..." << std::endl;
        }

        // Step 1: Function Set (8-bit mode, 2 lines, 5x8 font)
        auto result1 = lcd.exec(/*isData=*/false, /*read=*/false, /*value=*/0x38);
        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Step 1: Function Set (0x38) - " << (result1 ? "OK" : "FAILED") << std::endl;
        }

        // Small delay for command processing
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Step 2: Display ON/OFF Control (Display ON, Cursor OFF, Blink OFF)
        auto result2 = lcd.exec(/*isData=*/false, /*read=*/false, /*value=*/0x0C);
        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Step 2: Display ON (0x0C) - " << (result2 ? "OK" : "FAILED") << std::endl;
        }

        // Small delay for command processing
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Step 3: Clear Display
        auto result3 = lcd.exec(/*isData=*/false, /*read=*/false, /*value=*/0x01);
        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Step 3: Clear Display (0x01) - " << (result3 ? "OK" : "FAILED") << std::endl;
        }

        // Small delay for command processing
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        // Step 4: Entry Mode Set (Increment cursor, no display shift)
        auto result4 = lcd.exec(/*isData=*/false, /*read=*/false, /*value=*/0x06);
        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Step 4: Entry Mode Set (0x06) - " << (result4 ? "OK" : "FAILED") << std::endl;
        }

        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Bootstrap sequence completed after " << cfg.delay_ms << " ms delay" << std::endl;
        }
    } else {
        if (cfg.log) {
            std::cerr << "[LCD-BOOT] Display already ON - skipping bootstrap sequence" << std::endl;
        }
    }

    // Final status check
    if (cfg.log) {
        std::cerr << "[LCD-BOOT] Bootstrap diagnostics - AFTER:" << std::endl;
        std::cerr << "  Display ON: " << (lcd.isDisplayOn() ? "YES" : "NO") << std::endl;
        std::cerr << "  Current text line 0: '" << lcd.getText(0) << "'" << std::endl;
        std::cerr << "  Current text line 1: '" << lcd.getText(1) << "'" << std::endl;
        std::cerr << "  DDRAM content: ";
        const auto& ddram = lcd.getDdRam();
        for (size_t i = 0; i < ddram.size() && i < 20; ++i) {
            char c = ddram[i];
            std::cerr << (c >= 32 && c <= 126 ? c : '.');
        }
        std::cerr << std::endl;
        std::cerr << "[LCD-BOOT] Bootstrap diagnostics complete" << std::endl;
    }
}

} // namespace MS2000