#include "ms2000_switch_matrix.h"
#include <iostream>

namespace MS2000 {

MS2000SwitchMatrix::MS2000SwitchMatrix() {
    // Initialize with default state
    m_current_t_lines = 0x00;
    m_last_debounce_time = 0;
    m_last_d_state = 0x00;
}

void MS2000SwitchMatrix::setTLines(uint8_t t_value) {
    m_current_t_lines = t_value;
    
    if (m_debug_mode) {
        std::cout << "[Switch Matrix] T-Lines = 0x" << std::hex << (int)t_value 
                  << " (scanning row " << std::dec;
        
        // Show which row is being scanned
        for (int i = 0; i < 8; i++) {
            if (t_value & (1 << i)) {
                std::cout << i << " ";
            }
        }
        std::cout << ")" << std::endl;
    }
}

uint8_t MS2000SwitchMatrix::readDLines() {
    uint8_t d_state = 0x00;
    
    // For each active T-line (row), check if any buttons in that row are pressed
    for (int row = 0; row < 8; row++) {
        if (!(m_current_t_lines & (1 << row))) {
            continue; // This row is not selected
        }
        
        // Check all columns in this row
        for (int col = 0; col < COLUMNS; col++) {
            MS2000Button button_id = getButtonForRowCol(row, col);
            if (readSwitchState(button_id)) {
                d_state |= (1 << col); // Set corresponding D-line bit
            }
        }
    }
    
    if (m_debug_mode && d_state != 0) {
        std::cout << "[Switch Matrix] D-Lines = 0x" << std::hex << (int)d_state 
                  << " (pressed buttons in columns ";
        for (int i = 0; i < COLUMNS; i++) {
            if (d_state & (1 << i)) {
                std::cout << i << " ";
            }
        }
        std::cout << ")" << std::dec << std::endl;
    }
    
    return d_state;
}

void MS2000SwitchMatrix::processDebounce(uint32_t current_time_ms) {
    if (current_time_ms - m_last_debounce_time >= DEBOUNCE_TIME_MS) {
        // Time to process debouncing
        uint8_t current_d_state = readDLines();
        
        // Only update if state has been stable
        if (current_d_state != m_last_d_state) {
            m_last_d_state = current_d_state;
            m_last_debounce_time = current_time_ms;
            
            if (m_debug_mode) {
                std::cout << "[Switch Matrix] Debounced state = 0x" 
                         << std::hex << (int)current_d_state << std::dec << std::endl;
            }
        }
    }
}

MS2000Button MS2000SwitchMatrix::getButtonForRowCol(uint8_t row, uint8_t col) {
    // Map row (0..7) and col (0..5) to MS2000Button enum
    // Matrix layout: 8 rows x 6 columns = 48 buttons max
    uint8_t button_offset = (row * COLUMNS) + col;
    
    if (button_offset >= static_cast<uint8_t>(MS2000Button::BUTTON_COUNT)) {
        return MS2000Button::EXIT; // Default fallback
    }
    
    return static_cast<MS2000Button>(button_offset);
}

bool MS2000SwitchMatrix::readSwitchState(MS2000Button button_id) {
    if (m_switch_read_callback) {
        bool pressed = m_switch_read_callback(button_id);
        
        if (m_debug_mode && pressed) {
            const char* button_names[] = {
                "EXIT", "WRITE", "PAGE", "VR_PATCH_SOURCE",
                "VR_PATCH_DEST", "ARP_TYPE", "ARP_RANGE", "ARP_LATCH",
                "ARP_ON", "SEQ_EDIT", "SEQ_SELECT", "FILTER_TYPE",
                "LFO1_SELECT", "LFO2_SELECT", "PLUS_YES", "MINUS_NO",
                "CURSOR_LEFT", "CURSOR_RIGHT", "GLOBAL", "BANK_UP",
                "BANK_DOWN", "EDIT", "AMP_EG2", "AMP_GATE",
                "AMP_DISTORTION", "MOD_SEQ_REC", "MOD_SEQ_ON", "OSC1_WAVE",
                "OSC2_WAVE", "SYNC_RING", "VP_DEST", "FILTER_CUTOFF",
                "BUTTON_32", "BUTTON_33", "BUTTON_34", "BUTTON_35",
                "BUTTON_36", "BUTTON_37", "BUTTON_38", "BUTTON_39",
                "BUTTON_40", "BUTTON_41", "BUTTON_42", "BUTTON_43",
                "BUTTON_44", "BUTTON_45", "BUTTON_46", "BUTTON_47"
            };
            
            int button_idx = static_cast<int>(button_id);
            if (button_idx >= 0 && button_idx < 48) {
                std::cout << "[Switch Matrix] Button pressed: " << button_names[button_idx] 
                         << " (row=" << (button_idx / COLUMNS) 
                         << ", col=" << (button_idx % COLUMNS) << ")" << std::endl;
            }
        }
        
        return pressed;
    }
    
    // Default state if no callback set
    return false;
}

} // namespace MS2000