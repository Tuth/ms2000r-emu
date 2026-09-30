#pragma once

#include <cstdint>
#include <functional>

namespace MS2000 {

// MS2000 Switch Matrix System - 8 rows (T0..T7) x N columns (D0..Dn)
// Based on hardware specification: MCU drives T-lines, reads D-lines

enum class MS2000Button : uint8_t {
    // Main function buttons
    EXIT = 0,        WRITE = 1,       PAGE = 2,         VR_PATCH_SOURCE = 3,
    VR_PATCH_DEST = 4,  ARP_TYPE = 5,   ARP_RANGE = 6,    ARP_LATCH = 7,
    ARP_ON = 8,      SEQ_EDIT = 9,    SEQ_SELECT = 10,  FILTER_TYPE = 11,
    LFO1_SELECT = 12, LFO2_SELECT = 13, PLUS_YES = 14,    MINUS_NO = 15,
    
    // Navigation
    CURSOR_LEFT = 16,  CURSOR_RIGHT = 17, GLOBAL = 18,     BANK_UP = 19,
    BANK_DOWN = 20,    EDIT = 21,         AMP_EG2 = 22,    AMP_GATE = 23,
    AMP_DISTORTION = 24, MOD_SEQ_REC = 25, MOD_SEQ_ON = 26, OSC1_WAVE = 27,
    OSC2_WAVE = 28,    SYNC_RING = 29,   VP_DEST = 30,     FILTER_CUTOFF = 31,
    
    // Additional control buttons (extend as needed based on panel layout)
    BUTTON_32 = 32,   BUTTON_33 = 33,   BUTTON_34 = 34,   BUTTON_35 = 35,
    BUTTON_36 = 36,   BUTTON_37 = 37,   BUTTON_38 = 38,   BUTTON_39 = 39,
    BUTTON_40 = 40,   BUTTON_41 = 41,   BUTTON_42 = 42,   BUTTON_43 = 43,
    BUTTON_44 = 44,   BUTTON_45 = 45,   BUTTON_46 = 46,   BUTTON_47 = 47,
    
    BUTTON_COUNT = 48  // 6 columns x 8 rows = 48 buttons max
};

// Switch read callback: (button_id) -> true if pressed
using SwitchReadCallback = std::function<bool(MS2000Button button_id)>;

class MS2000SwitchMatrix {
public:
    MS2000SwitchMatrix();
    
    // Set GUI callback for reading switch states
    void setSwitchReadCallback(SwitchReadCallback callback) { m_switch_read_callback = callback; }
    
    // MCU drives T0..T7 lines (row select) and reads D0..D5 lines (column data)
    void setTLines(uint8_t t_value);  // Write to T0..T7 (row select)
    uint8_t readDLines();             // Read from D0..D5 (column data)
    
    // Get current T-line value
    uint8_t getTLines() const { return m_current_t_lines; }
    
    // Process debouncing - call periodically (every 5-10ms)
    void processDebounce(uint32_t current_time_ms);
    
    // Debug info
    void setDebugMode(bool enabled) { m_debug_mode = enabled; }

private:
    uint8_t m_current_t_lines = 0x00;     // Current T0..T7 state (row select)
    SwitchReadCallback m_switch_read_callback = nullptr;
    bool m_debug_mode = false;
    
    // Debouncing state
    uint32_t m_last_debounce_time = 0;
    uint8_t m_last_d_state = 0x00;
    
    static constexpr uint32_t DEBOUNCE_TIME_MS = 10; // 10ms debounce
    static constexpr uint8_t COLUMNS = 6;            // D0..D5 = 6 columns max
    
    // Get button ID for given row and column
    MS2000Button getButtonForRowCol(uint8_t row, uint8_t col);
    
    // Read switch state with callback or return default
    bool readSwitchState(MS2000Button button_id);
};

} // namespace MS2000