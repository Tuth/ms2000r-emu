#pragma once

#include <cstdint>
#include <functional>
#include <array>

namespace MS2000 {

// MS2000 LED Matrix System - 2-color LEDs (~98 LEDs total)
// Based on hardware specification: ADSEL0..1 (row select) + LDD0..LDD5 (column data)

enum class MS2000LED : uint8_t {
    // LFO1 indicators
    LFO1_SQU = 0,    LFO1_SAW = 1,    LFO1_SIN = 2,    LFO1_TRI = 3,
    
    // Filter type indicators  
    FILTER_LPF = 4,  FILTER_BPF = 5,  FILTER_HPF = 6,  FILTER_24DB_LPF = 7,
    
    // OSC1 wave indicators
    OSC1_TRI = 8,    OSC1_SAW = 9,    OSC1_SQ = 10,    OSC1_NOISE = 11,
    OSC1_SYNC = 12,  OSC1_RING = 13,
    
    // LFO2 indicators
    LFO2_SQU = 14,   LFO2_SAW = 15,   LFO2_SIN = 16,   LFO2_TRI = 17,
    
    // OSC2 wave indicators
    OSC2_TRI = 18,   OSC2_SAW = 19,   OSC2_SQ = 20,    OSC2_NOISE = 21,
    OSC2_SYNC = 22,  OSC2_RING = 23,
    
    // VP (Virtual Patch) destination indicators
    VP_DEST_1 = 24,  VP_DEST_2 = 25,  VP_DEST_3 = 26,  VP_DEST_4 = 27,
    VP_DEST_5 = 28,  VP_DEST_6 = 29,  VP_DEST_7 = 30,  VP_DEST_8 = 31,
    
    // ARP (Arpeggiator) indicators
    ARP_UP = 32,     ARP_DOWN = 33,   ARP_ALT1 = 34,   ARP_ALT2 = 35,
    ARP_RANDOM = 36, ARP_TRIGGER = 37,
    
    // EG (Envelope Generator) indicators
    EG1_GATE = 38,   EG1_EG2 = 39,    EG1_LFO1 = 40,   EG1_LFO2 = 41,
    EG2_GATE = 42,   EG2_EG1 = 43,    EG2_LFO1 = 44,   EG2_LFO2 = 45,
    
    // Modulation indicators
    MOD_WHEEL = 46,  MOD_AFTER = 47,  MOD_EG1 = 48,    MOD_EG2 = 49,
    MOD_LFO1 = 50,   MOD_LFO2 = 51,   MOD_VELO = 52,   MOD_KEY = 53,
    
    // Effect indicators
    EFF_CHORUS = 54, EFF_ENSEMBLE = 55, EFF_PHASER = 56, EFF_FLANGER = 57,
    EFF_DELAY = 58,  EFF_REVERB = 59,
    
    // Additional indicators (extend based on panel layout)
    LED_60 = 60,     LED_61 = 61,     LED_62 = 62,     LED_63 = 63,
    LED_64 = 64,     LED_65 = 65,     LED_66 = 66,     LED_67 = 67,
    LED_68 = 68,     LED_69 = 69,     LED_70 = 70,     LED_71 = 71,
    LED_72 = 72,     LED_73 = 73,     LED_74 = 74,     LED_75 = 75,
    LED_76 = 76,     LED_77 = 77,     LED_78 = 78,     LED_79 = 79,
    LED_80 = 80,     LED_81 = 81,     LED_82 = 82,     LED_83 = 83,
    LED_84 = 84,     LED_85 = 85,     LED_86 = 86,     LED_87 = 87,
    LED_88 = 88,     LED_89 = 89,     LED_90 = 90,     LED_91 = 91,
    LED_92 = 92,     LED_93 = 93,     LED_94 = 94,     LED_95 = 95,
    LED_96 = 96,     LED_97 = 97,
    
    LED_COUNT = 98   // Total ~98 LEDs
};

struct LEDColor {
    uint8_t red = 0;    // 0..255
    uint8_t green = 0;  // 0..255
};

// LED update callback: (led_id, color) -> update GUI display
using LEDUpdateCallback = std::function<void(MS2000LED led_id, const LEDColor& color)>;

class MS2000LEDMatrix {
public:
    MS2000LEDMatrix();
    
    // Set GUI callback for LED updates
    void setLEDUpdateCallback(LEDUpdateCallback callback) { m_led_update_callback = callback; }
    
    // MCU controls via ADSEL0..1 (row select) and LDD0..LDD5 (column data)
    void setADSEL01(uint8_t adsel_value);  // ADSEL0..1 for row select (0..3)
    void setLDD(uint8_t ldd_value);        // LDD0..LDD5 column data
    
    // Update display - call periodically to refresh LEDs
    void updateDisplay();
    
    // Direct LED control (for GUI/testing)
    void setLED(MS2000LED led_id, const LEDColor& color);
    void setLED(MS2000LED led_id, uint8_t red, uint8_t green);
    LEDColor getLED(MS2000LED led_id) const;
    
    // Clear all LEDs
    void clearAllLEDs();
    
    // Debug info
    void setDebugMode(bool enabled) { m_debug_mode = enabled; }

private:
    std::array<LEDColor, static_cast<size_t>(MS2000LED::LED_COUNT)> m_led_states;
    LEDUpdateCallback m_led_update_callback = nullptr;
    bool m_debug_mode = false;
    
    // Current control state
    uint8_t m_current_adsel01 = 0;  // ADSEL0..1 (0..3 for 4 rows)
    uint8_t m_current_ldd = 0;      // LDD0..LDD5 column data
    
    static constexpr uint8_t ROWS = 4;     // ADSEL0..1 = 4 rows max
    static constexpr uint8_t COLUMNS = 6;  // LDD0..LDD5 = 6 columns max
    
    // Get LED ID for given row and column
    MS2000LED getLEDForRowCol(uint8_t row, uint8_t col);
    
    // Notify GUI of LED changes
    void notifyLEDUpdate(MS2000LED led_id);
};

} // namespace MS2000