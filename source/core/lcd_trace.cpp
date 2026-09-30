#include "lcd_trace.h"
#include <iostream>
#include <atomic>
#include <cstdlib>

// BUG64: this was `true`, i.e. an UNCONDITIONAL printf on every byte the display
// path carries. Measured on the first boot that reached the normal firmware:
// 53,518 lines out of a 937,189-line log, on a path the firmware runs ~2,200
// times a second. *An instrument on an always-running path is itself an
// intervention* - the rule this thread paid for on the S3000XL, where per-access
// probes slowed the emulation enough that Thor could HEAR it, and paid again
// here when [RTS-EXEC] was 94% of a 5.5 M-line log (BUG62).
//
// Default OFF (R2). MS2K_LCDTRACE=1 turns it on together with the [LCD-P2DR#]
// stream in writeP2DR(), so one switch gives the whole display path.
std::atomic<bool> g_lcdTraceOn(false);
static bool lcdTraceEnvOn() {
    static int on = -1;
    if (on < 0) {
        const char* e = std::getenv("MS2K_LCDTRACE");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on != 0;
}

void lcd_trace(bool isData, uint8_t val, LcdPath path) {
    if (!g_lcdTraceOn.load() && !lcdTraceEnvOn()) return;

    const char* pathStr;
    switch (path) {
        case LcdPath::GPIO:     pathStr = "GPIO"; break;
        case LcdPath::PANEL_MP: pathStr = "PANEL_MP"; break;
        case LcdPath::DIRECT:   pathStr = "DIRECT"; break;
        default:                pathStr = "UNKNOWN"; break;
    }

    const char* typeStr = isData ? "DATA" : "CMD";

    std::cout << "[LCD] " << pathStr << " " << typeStr << " 0x"
              << std::hex << std::uppercase << static_cast<int>(val)
              << std::dec;

    // Add ASCII representation for printable characters
    if (isData && val >= 32 && val <= 126) {
        std::cout << " ('" << static_cast<char>(val) << "')";
    }

    std::cout << std::endl;
}