#pragma once
#include <cstdint>

struct LcdBootstrapConfig {
    bool enabled = true;           // dev/ci default: ON (nem zavarja a FW-t, a 0x0C idempotens)
    int  delay_ms = 25;            // add nyugalmi időt a bekapcs után
    bool log = true;
};

namespace MS2000 {
    class RealLCDDisplay;
    void bootstrap_lcd_display_on(RealLCDDisplay& lcd, const LcdBootstrapConfig& cfg);
}