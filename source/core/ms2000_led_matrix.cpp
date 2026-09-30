#include "ms2000_led_matrix.h"
#include <iostream>
#include <cstring>

namespace MS2000 {

MS2000LEDMatrix::MS2000LEDMatrix() {
    // Initialize all LEDs to off
    clearAllLEDs();
    m_current_adsel01 = 0;
    m_current_ldd = 0;
}

void MS2000LEDMatrix::setADSEL01(uint8_t adsel_value) {
    m_current_adsel01 = adsel_value & 0x03; // Only 2 bits used (0..3)
    
    if (m_debug_mode) {
        std::cout << "[LED Matrix] ADSEL0..1 = " << (int)m_current_adsel01 
                  << " (scanning row " << (int)m_current_adsel01 << ")" << std::endl;
    }
}

void MS2000LEDMatrix::setLDD(uint8_t ldd_value) {
    m_current_ldd = ldd_value & 0x3F; // LDD0..LDD5 = 6 bits
    
    if (m_debug_mode) {
        std::cout << "[LED Matrix] LDD0..5 = 0x" << std::hex << (int)ldd_value 
                  << " (columns ";
        for (int i = 0; i < COLUMNS; i++) {
            if (ldd_value & (1 << i)) {
                std::cout << i << " ";
            }
        }
        std::cout << "on)" << std::dec << std::endl;
    }
    
    // Update LEDs in current row based on LDD data
    updateDisplay();
}

void MS2000LEDMatrix::updateDisplay() {
    // Update LEDs in the current row based on LDD column data
    for (int col = 0; col < COLUMNS; col++) {
        MS2000LED led_id = getLEDForRowCol(m_current_adsel01, col);
        
        if (led_id < MS2000LED::LED_COUNT) {
            // Set LED color based on LDD bit
            LEDColor color;
            if (m_current_ldd & (1 << col)) {
                // LED is on - set to appropriate color
                color.red = 255;   // Full red for now (can be customized)
                color.green = 0;
            } else {
                // LED is off
                color.red = 0;
                color.green = 0;
            }
            
            // Only update if color changed
            if (m_led_states[static_cast<size_t>(led_id)].red != color.red ||
                m_led_states[static_cast<size_t>(led_id)].green != color.green) {
                
                m_led_states[static_cast<size_t>(led_id)] = color;
                notifyLEDUpdate(led_id);
            }
        }
    }
}

void MS2000LEDMatrix::setLED(MS2000LED led_id, const LEDColor& color) {
    if (led_id < MS2000LED::LED_COUNT) {
        m_led_states[static_cast<size_t>(led_id)] = color;
        notifyLEDUpdate(led_id);
    }
}

void MS2000LEDMatrix::setLED(MS2000LED led_id, uint8_t red, uint8_t green) {
    LEDColor color = {red, green};
    setLED(led_id, color);
}

LEDColor MS2000LEDMatrix::getLED(MS2000LED led_id) const {
    if (led_id < MS2000LED::LED_COUNT) {
        return m_led_states[static_cast<size_t>(led_id)];
    }
    return {0, 0}; // Off
}

void MS2000LEDMatrix::clearAllLEDs() {
    for (auto& led : m_led_states) {
        led.red = 0;
        led.green = 0;
    }
    
    // Notify GUI of all LED changes
    if (m_led_update_callback) {
        for (size_t i = 0; i < static_cast<size_t>(MS2000LED::LED_COUNT); i++) {
            m_led_update_callback(static_cast<MS2000LED>(i), {0, 0});
        }
    }
    
    if (m_debug_mode) {
        std::cout << "[LED Matrix] All LEDs cleared" << std::endl;
    }
}

MS2000LED MS2000LEDMatrix::getLEDForRowCol(uint8_t row, uint8_t col) {
    // Map row (0..3) and col (0..5) to MS2000LED enum
    // Matrix layout: 4 rows x 6 columns = 24 base positions
    // But we have ~98 LEDs total, so we need a more complex mapping
    
    uint8_t led_offset = (row * COLUMNS) + col;
    
    // For now, use simple linear mapping
    // In real hardware, this would be based on the actual LED matrix wiring
    if (led_offset >= static_cast<uint8_t>(MS2000LED::LED_COUNT)) {
        return MS2000LED::LFO1_SQU; // Default fallback
    }
    
    return static_cast<MS2000LED>(led_offset);
}

void MS2000LEDMatrix::notifyLEDUpdate(MS2000LED led_id) {
    if (m_led_update_callback && led_id < MS2000LED::LED_COUNT) {
        const LEDColor& color = m_led_states[static_cast<size_t>(led_id)];
        m_led_update_callback(led_id, color);
        
        if (m_debug_mode && (color.red > 0 || color.green > 0)) {
            const char* led_names[] = {
                "LFO1_SQU", "LFO1_SAW", "LFO1_SIN", "LFO1_TRI",
                "FILTER_LPF", "FILTER_BPF", "FILTER_HPF", "FILTER_24DB_LPF",
                "OSC1_TRI", "OSC1_SAW", "OSC1_SQ", "OSC1_NOISE", "OSC1_SYNC", "OSC1_RING",
                "LFO2_SQU", "LFO2_SAW", "LFO2_SIN", "LFO2_TRI",
                "OSC2_TRI", "OSC2_SAW", "OSC2_SQ", "OSC2_NOISE", "OSC2_SYNC", "OSC2_RING",
                "VP_DEST_1", "VP_DEST_2", "VP_DEST_3", "VP_DEST_4",
                "VP_DEST_5", "VP_DEST_6", "VP_DEST_7", "VP_DEST_8",
                "ARP_UP", "ARP_DOWN", "ARP_ALT1", "ARP_ALT2", "ARP_RANDOM", "ARP_TRIGGER",
                "EG1_GATE", "EG1_EG2", "EG1_LFO1", "EG1_LFO2",
                "EG2_GATE", "EG2_EG1", "EG2_LFO1", "EG2_LFO2",
                "MOD_WHEEL", "MOD_AFTER", "MOD_EG1", "MOD_EG2",
                "MOD_LFO1", "MOD_LFO2", "MOD_VELO", "MOD_KEY",
                "EFF_CHORUS", "EFF_ENSEMBLE", "EFF_PHASER", "EFF_FLANGER",
                "EFF_DELAY", "EFF_REVERB"
                // Add more names as needed
            };
            
            int led_idx = static_cast<int>(led_id);
            const char* led_name = (led_idx < 58) ? led_names[led_idx] : "LED_UNKNOWN";
            
            std::cout << "[LED Matrix] " << led_name << " = RGB(" 
                     << (int)color.red << "," << (int)color.green << ")" << std::endl;
        }
    }
}

} // namespace MS2000