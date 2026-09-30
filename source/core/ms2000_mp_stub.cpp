#include "ms2000_mp_stub.h"
#include <iostream>
#include <algorithm>

namespace MS2000 {

// ==== Constructor ====

MS2000MPStub::MS2000MPStub()
    : m_lcd_display(nullptr)
    , m_cycle_counter(0)
    , m_lcd_busy(false)
    , m_lcd_command_buffer(0)
    , m_lcd_data_buffer(0)
    , m_sci_state(SciProtocolState::CMD)
    , m_last_activity_time(0)
    , m_auto_detection_enabled(true)
    , m_gpio_mode_active(true)
    , m_sci_activity_count(0)
    , m_gpio_activity_count(0)
{
    // Initialize LCD interface
    m_lcd.data_bus = 0;
    m_lcd.rs = false;
    m_lcd.rw = false;
    m_lcd.enable = false;
    m_lcd.busy = false;
    m_lcd.cursor_position = 0;
    m_lcd.display_lines[0] = "";
    m_lcd.display_lines[1] = "";
    
    // Initialize potentiometer interface
    m_pots.mux_channel = 0;
    m_pots.adc_value = 0x200; // Middle position
    m_pots.pot_values.fill(0x200);
    
    // Initialize button matrix
    m_buttons.row_select = 0;
    m_buttons.column_data = 0xFF; // All buttons released
    m_buttons.button_states.fill(0xFF);
    
    // Initialize MP registers
    m_registers.lcd_control = 0;
    m_registers.lcd_data = 0;
    m_registers.lcd_status = 0x80; // Busy flag set initially
    m_registers.adc_control = 0;
    m_registers.adc_data_low = 0;
    m_registers.adc_data_high = 0;
    m_registers.button_control = 0;
    m_registers.button_data = 0xFF;
    
    // Initialize RTC registers to a reasonable default (2000-01-01 00:00:00, Saturday)
    m_registers.rtc_sec = 0;
    m_registers.rtc_min = 0;
    m_registers.rtc_hour = 0;
    m_registers.rtc_wday = 6; // 0=Sun, 6=Sat
    m_registers.rtc_day = 1;
    m_registers.rtc_month = 1;
    m_registers.rtc_year = 0; // Years since 2000
    m_registers.rtc_ctrl = 0x01; // RTC enabled
    
    // Initialize knob mapping system
    m_knob_mapping = std::make_unique<MS2000KnobMapping>();
    
    std::cout << "MS2000 MP Stub initialized (CPU → MP → LCD architecture)" << std::endl;
}

// ==== CPU Interface ====

uint8_t MS2000MPStub::readRegister(uint32_t address)
{
    // Address decoding for MP registers
    switch (address & 0xFF) {
        case 0x60: // LCD Control Register
            return m_registers.lcd_control;
            
        case 0x61: // LCD Data Register
            return m_registers.lcd_data;
            
        case 0x62: // LCD Status Register
            // Update busy flag based on internal state
            m_registers.lcd_status = m_lcd_busy ? 0x80 : 0x00;
            return m_registers.lcd_status;
            
        case 0x63: // ADC Control Register
            return m_registers.adc_control;
            
        case 0x64: // ADC Data Low
            return m_registers.adc_data_low;
            
        case 0x65: // ADC Data High
            return m_registers.adc_data_high;
            
        case 0x66: // Button Matrix Control
            return m_registers.button_control;
            
        case 0x67: // Button Matrix Data
            return m_registers.button_data;
            
        // RTC Registers (0x68-0x6F)
        case 0x68: // RTC Second Counter
            return m_registers.rtc_sec;
            
        case 0x69: // RTC Minute Counter
            return m_registers.rtc_min;
            
        case 0x6A: // RTC Hour Counter
            return m_registers.rtc_hour;
            
        case 0x6B: // RTC Day of Week
            return m_registers.rtc_wday;
            
        case 0x6C: // RTC Day Counter
            return m_registers.rtc_day;
            
        case 0x6D: // RTC Month Counter
            return m_registers.rtc_month;
            
        case 0x6E: // RTC Year Counter (years since 2000)
            return m_registers.rtc_year;
            
        case 0x6F: // RTC Control Register
            return m_registers.rtc_ctrl;
            
        default:
            return 0xFF; // Default value for unmapped addresses
    }
}

// BUG70b: MS2K_MPSTUB=1 to see this stub's traffic. It was two UNCONDITIONAL
// cout lines per write - measured 27,306 + 27,305 = 54,611 lines in a 25 s boot,
// 94% of the whole log, on the P1DR path. The same defect family as BUG64.
static bool mpStubTrace() {
    static int on = -1;
    if (on < 0) { const char* e = std::getenv("MS2K_MPSTUB"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on != 0;
}

void MS2000MPStub::writeRegister(uint32_t address, uint8_t value)
{
    if (mpStubTrace()) {
        std::cout << "[MP STUB] WRITE: 0x" << std::hex << address
                  << " = 0x" << (int)value << std::dec << std::endl;
    }

    // Address decoding for MP registers
    switch (address & 0xFF) {
        case 0x60:
            // =============================================================
            // BUG70b - THIS IS P1DR, AND CALLING IT "LCD Control" IS THE LAST
            // ECHO OF THE FICTION BUG46 AND BUG48 REMOVED.
            //
            // KOD-A30411: the LCD is on PORT 2 (P20-P26), and `writeP2DR()`
            // owns it. `0x60` is P1DR. BUG48 cut the route that handed P1DR to
            // the display as a finished command; what survived is this decode,
            // which still splits the byte into RS/RW/E and PRINTS THEM AS LCD
            // SIGNALS - 27,305 times in a 25 s run.
            //
            // Storage is kept (an unmapped register is destructive, not
            // neutral), the RS/RW/E fields are kept because the stub's own read
            // path uses them, and the LABEL is corrected. What Port 1 actually
            // drives has not been read off the schematic yet - that is an open
            // question, and mislabelling it as the display is how it stayed one.
            // =============================================================
            m_registers.lcd_control = value;
            m_lcd.rs = (value & 0x01) != 0;
            m_lcd.rw = (value & 0x02) != 0;
            m_lcd.enable = (value & 0x04) != 0;
            if (mpStubTrace()) {
                std::cout << "[MP STUB] P1DR (NOT the LCD - see BUG70b) = 0x"
                          << std::hex << (int)value << std::dec << std::endl;
            }
            break;
            
        case 0x61: // LCD Data Register
            m_registers.lcd_data = value;
            std::cout << "[MP STUB] LCD Data: 0x" << std::hex << (int)value 
                      << " RS=" << m_lcd.rs << std::dec << std::endl;
            // Process LCD data based on RS signal
            if (m_lcd.rs) {
                // Data write
                std::cout << "[MP STUB] Processing LCD DATA: 0x" << std::hex << (int)value << std::dec << std::endl;
                processLCDData(value);
            } else {
                // Command write
                std::cout << "[MP STUB] Processing LCD COMMAND: 0x" << std::hex << (int)value << std::dec << std::endl;
                processLCDCommand(value);
            }
            break;
            
        case 0x63: // ADC Control Register
            m_registers.adc_control = value;
            // Set multiplexer channel
            m_pots.mux_channel = value & 0x1F; // 5-bit channel select
            updateADC();
            break;
            
        case 0x66: // Button Matrix Control
            m_registers.button_control = value;
            // Set row select for button matrix scanning
            m_buttons.row_select = value & 0x07; // 3-bit row select
            updateButtonMatrix();
            break;
            
        default:
            // Ignore writes to unmapped addresses
            break;
            
        // RTC Registers (0x68-0x6F) - Write handling
        case 0x68: // RTC Second Counter
            if (value < 60) m_registers.rtc_sec = value;
            std::cout << "[MP STUB] RTC Second: " << (int)m_registers.rtc_sec << std::endl;
            break;
            
        case 0x69: // RTC Minute Counter
            if (value < 60) m_registers.rtc_min = value;
            std::cout << "[MP STUB] RTC Minute: " << (int)m_registers.rtc_min << std::endl;
            break;
            
        case 0x6A: // RTC Hour Counter
            if (value < 24) m_registers.rtc_hour = value;
            std::cout << "[MP STUB] RTC Hour: " << (int)m_registers.rtc_hour << std::endl;
            break;
            
        case 0x6B: // RTC Day of Week
            if (value < 7) m_registers.rtc_wday = value;
            std::cout << "[MP STUB] RTC Weekday: " << (int)m_registers.rtc_wday << std::endl;
            break;
            
        case 0x6C: // RTC Day Counter
            if (value >= 1 && value <= 31) m_registers.rtc_day = value;
            std::cout << "[MP STUB] RTC Day: " << (int)m_registers.rtc_day << std::endl;
            break;
            
        case 0x6D: // RTC Month Counter
            if (value >= 1 && value <= 12) m_registers.rtc_month = value;
            std::cout << "[MP STUB] RTC Month: " << (int)m_registers.rtc_month << std::endl;
            break;
            
        case 0x6E: // RTC Year Counter (years since 2000)
            m_registers.rtc_year = value & 0x7F; // 7 bits for year
            std::cout << "[MP STUB] RTC Year: 20" << (int)(m_registers.rtc_year) << std::endl;
            break;
            
        case 0x6F: // RTC Control Register
            m_registers.rtc_ctrl = value;
            std::cout << "[MP STUB] RTC Control: 0x" << std::hex << (int)value << std::dec << std::endl;
            break;
    }
}

// ==== SCI Communication Interface ====

void MS2000MPStub::onCpuTxByte(uint8_t data)
{
    m_last_activity_time = m_cycle_counter;

    // Enhanced SCI trace logging with protocol state
    std::cout << "[SCI1→MP] TX 0x" << std::hex << (int)data << std::dec;
    if (data >= 32 && data <= 126) {
        std::cout << " ('" << (char)data << "')";
    }
    std::cout << " [state:" << (m_sci_state == SciProtocolState::CMD ? "CMD" : "DATA") << "]" << std::endl;

    // Heuristic SCI protocol translation to HD44780
    // Enhanced detection based on MS2000 firmware patterns and HD44780 spec

    bool is_command = false;
    bool is_address_command = false;

    // Primary command detection - standard HD44780 commands
    switch (data) {
        case 0x01: // Clear Display
        case 0x02: // Return Home
        case 0x03: // Entry Mode Set (rare)
        case 0x04: // Display ON/OFF Control (rare)
        case 0x05: // Cursor/Display Shift (rare)
        case 0x06: // Entry Mode Set (increment, no shift)
        case 0x07: // Display ON/OFF (rare)
        case 0x08: // Display/Cursor OFF
        case 0x0C: // Display ON, Cursor OFF, Blink OFF
        case 0x0E: // Display ON, Cursor ON, Blink OFF
        case 0x0F: // Display ON, Cursor ON, Blink ON
        case 0x10: // Cursor Shift Left
        case 0x14: // Cursor Shift Right
        case 0x18: // Display Shift Left
        case 0x1C: // Display Shift Right
        case 0x28: // Function Set (4-bit, 2-line, 5x8)
        case 0x30: // Function Set (8-bit, 1-line, 5x8)
        case 0x34: // Function Set (8-bit, 2-line, 5x8)
        case 0x38: // Function Set (8-bit, 2-line, 5x8)
        case 0x3C: // Function Set (8-bit, 2-line, 5x10)
            is_command = true;
            break;
        default:
            // Address commands - DDRAM/CGRAM addressing
            if ((data & 0x80) == 0x80) { // Bit 7 set = address command
                is_command = true;
                is_address_command = true;
            }
            break;
    }

    // Secondary heuristic: if data looks like ASCII text and we're in data mode, treat as data
    if (!is_command && m_sci_state == SciProtocolState::DATA) {
        if ((data >= 0x20 && data <= 0x7E) || data == 0x00 || data == 0x0A || data == 0x0D) {
            is_command = false; // Keep as data
        }
    }

    // Update protocol state machine
    if (is_command) {
        m_sci_state = SciProtocolState::CMD;
        if (is_address_command) {
            std::cout << "[LCD] PANEL ADDRESS CMD 0x" << std::hex << (int)data << std::dec << std::endl;
        } else {
            std::cout << "[LCD] PANEL CMD 0x" << std::hex << (int)data << std::dec << std::endl;
        }
        processLCDCommand(data);
    } else {
        m_sci_state = SciProtocolState::DATA;
        std::cout << "[LCD] PANEL DATA 0x" << std::hex << (int)data << std::dec;
        if (data >= 32 && data <= 126) {
            std::cout << " ('" << (char)data << "')";
        }
        std::cout << std::endl;
        processLCDData(data);
    }

    // Auto-detection: if we see LCD activity on SCI, switch to Panel-MP mode
    if (m_auto_detection_enabled && m_gpio_mode_active) {
        m_sci_activity_count++;
        if (m_sci_activity_count >= 3) { // 3 consecutive SCI LCD operations
            switchToPanelMPMode();
        }
    }
}

bool MS2000MPStub::hasRxByte(uint8_t& data)
{
    if (!m_rx_buffer.empty()) {
        data = m_rx_buffer.front();
        m_rx_buffer.erase(m_rx_buffer.begin());

        // SCI trace logging
        std::cout << "[MP→SCI1] RX 0x" << std::hex << (int)data << std::dec;
        if (data >= 32 && data <= 126) {
            std::cout << " ('" << (char)data << "')";
        }
        std::cout << std::endl;

        return true;
    }
    return false;
}

// ==== LCD Display Connection ====

void MS2000MPStub::setLCDDisplay(RealLCDDisplay* lcd_display)
{
    m_lcd_display = lcd_display;
    std::cout << "MP Stub: LCD display connected" << std::endl;
}

// ==== Button Matrix Simulation ====

void MS2000MPStub::setButtonState(uint8_t row, uint8_t col, bool pressed)
{
    if (row < 8 && col < 8) {
        if (pressed) {
            m_buttons.button_states[row] &= ~(1 << col); // Clear bit (button pressed)
        } else {
            m_buttons.button_states[row] |= (1 << col);  // Set bit (button released)
        }
    }
}

// ==== Potentiometer Simulation ====

void MS2000MPStub::setPotentiometerValue(uint8_t channel, uint16_t value)
{
    if (channel < 32) {
        m_pots.pot_values[channel] = value & 0x3FF; // 10-bit ADC value
    }
}

// ==== Internal Processing ====

void MS2000MPStub::update(uint32_t cycles)
{
    m_cycle_counter += cycles;
    
    // Update LCD timing
    if (m_lcd_busy) {
        // Simulate LCD busy time (typically 37-40 microseconds for commands)
        if (m_cycle_counter >= 1000) { // Simplified timing
            m_lcd_busy = false;
            m_cycle_counter = 0;
        }
    }
    
    // Update ADC conversion timing
    updateADC();
    
    // Update button matrix scanning
    updateButtonMatrix();
}

// ==== Internal LCD Processing ====

void MS2000MPStub::processLCDCommand(uint8_t command)
{
    std::cout << "[MP STUB] LCD Command: 0x" << std::hex << (int)command << std::dec << std::endl;
    
    // HD44780 LCD command processing
    switch (command) {
        case 0x01: // Clear Display
            std::cout << "[MP STUB] LCD Clear Display" << std::endl;
            m_lcd.display_lines[0] = "";
            m_lcd.display_lines[1] = "";
            m_lcd.cursor_position = 0;
            m_lcd_busy = true;
            m_cycle_counter = 0;
            break;
            
        case 0x02: // Return Home
            m_lcd.cursor_position = 0;
            m_lcd_busy = true;
            m_cycle_counter = 0;
            break;
            
        case 0x06: // Entry Mode Set (Increment cursor, no display shift)
            // Default behavior, no action needed
            break;
            
        case 0x0C: // Display ON/OFF Control (Display ON, cursor OFF, blink OFF)
            // Display is always on in emulation
            break;
            
        case 0x28: // Function Set (4-bit mode, 2 lines, 5x8 font)
            // Default configuration
            break;
            
        case 0x80: // Set DDRAM Address (Line 1, position 0)
        case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
            // Set cursor position (Line 1: 0x80-0x9F)
            m_lcd.cursor_position = command & 0x1F;
            break;
            
        case 0xC0: // Set DDRAM Address (Line 2, position 0)
        case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            // Set cursor position (Line 2: 0xC0-0xDF)
            m_lcd.cursor_position = (command & 0x1F) + 32; // Offset for line 2
            break;
            
        default:
            // Unknown command, ignore
            break;
    }
    
    // Update external LCD display
    updateLCDDisplay();
}

void MS2000MPStub::processLCDData(uint8_t data)
{
    std::cout << "[MP STUB] LCD Data: 0x" << std::hex << (int)data 
              << " ('" << (char)((data >= 32 && data <= 126) ? data : '?') << "')" << std::dec << std::endl;
    
    // Write character to current cursor position
    int line = (m_lcd.cursor_position >= 32) ? 1 : 0;
    int pos = m_lcd.cursor_position % 32;
    
    std::cout << "[MP STUB] Writing to line " << line << " position " << pos << std::endl;
    
    // Ensure line has enough characters
    if (m_lcd.display_lines[line].length() <= pos) {
        m_lcd.display_lines[line].resize(pos + 1, ' ');
    }
    
    // Write character (convert to printable ASCII)
    char c = (data >= 32 && data <= 126) ? static_cast<char>(data) : '?';
    m_lcd.display_lines[line][pos] = c;
    
    // Increment cursor position
    m_lcd.cursor_position++;
    if (m_lcd.cursor_position >= 64) { // Wrap around
        m_lcd.cursor_position = 0;
    }
    
    std::cout << "[MP STUB] Display lines: '" << m_lcd.display_lines[0] << "' | '" << m_lcd.display_lines[1] << "'" << std::endl;
    
    // Update external LCD display
    updateLCDDisplay();
}

void MS2000MPStub::updateLCDDisplay()
{
    if (m_lcd_display) {
        m_lcd_display->setText(0, m_lcd.display_lines[0]);
        m_lcd_display->setText(1, m_lcd.display_lines[1]);
    }
}

// ==== Internal ADC Processing ====

void MS2000MPStub::updateADC()
{
    // Get current potentiometer value
    if (m_pots.mux_channel < 32) {
        m_pots.adc_value = m_pots.pot_values[m_pots.mux_channel];
    }
    
    // Update ADC data registers
    m_registers.adc_data_low = m_pots.adc_value & 0xFF;
    m_registers.adc_data_high = (m_pots.adc_value >> 8) & 0x03; // 10-bit value
}

// ==== Internal Button Matrix Processing ====

void MS2000MPStub::updateButtonMatrix()
{
    // Get column data for current row
    if (m_buttons.row_select < 8) {
        m_buttons.column_data = m_buttons.button_states[m_buttons.row_select];
    }
    
    // Update button data register
    m_registers.button_data = m_buttons.column_data;
}

// ==== Address Decoding ====

bool MS2000MPStub::isLCDAddress(uint32_t address) const
{
    return (address & 0xFF) >= 0x60 && (address & 0xFF) <= 0x62;
}

bool MS2000MPStub::isADCAddress(uint32_t address) const
{
    return (address & 0xFF) >= 0x63 && (address & 0xFF) <= 0x65;
}

bool MS2000MPStub::isButtonAddress(uint32_t address) const
{
    return (address & 0xFF) >= 0x66 && (address & 0xFF) <= 0x67;
}

// ==== Knob Mapping Interface ====

void MS2000MPStub::setKnobValue(uint8_t knob_id, uint8_t value)
{
    if (m_knob_mapping) {
        m_knob_mapping->setKnobValue(knob_id, value);
        
        // Also update ADC value if this knob is currently selected
        uint8_t adc_channel = m_knob_mapping->knobIdToADCChannel(knob_id);
        if (adc_channel == m_pots.mux_channel) {
            // Convert 7-bit knob value (0-127) to 10-bit ADC value (0-1023)
            uint16_t adc_value = (value * 1023) / 127;
            m_pots.pot_values[adc_channel] = adc_value;
            updateADC();
        }
    }
}

uint8_t MS2000MPStub::getKnobValue(uint8_t knob_id) const
{
    if (m_knob_mapping) {
        return m_knob_mapping->getKnobValue(knob_id);
    }
    return 64; // Default middle value
}

void MS2000MPStub::setKnobMode(KnobMode mode)
{
    if (m_knob_mapping) {
        m_knob_mapping->setCurrentMode(mode);
        std::cout << "[MP STUB] Knob mode changed to: " << static_cast<int>(mode) << std::endl;
    }
}

KnobMode MS2000MPStub::getKnobMode() const
{
    if (m_knob_mapping) {
        return m_knob_mapping->getCurrentMode();
    }
    return KnobMode::PARAM_EDIT;
}

// ==== ADC Interface for Firmware Simulation ====

void MS2000MPStub::setADCMuxChannel(uint8_t channel)
{
    m_pots.mux_channel = channel & 0x1F; // 0-31 channels
    updateADC();
    std::cout << "[MP STUB] ADC Mux Channel set to: " << (int)m_pots.mux_channel << std::endl;
}

uint16_t MS2000MPStub::readADC() const
{
    return m_pots.adc_value;
}

// ==== Auto-detection Implementation ====

void MS2000MPStub::enableAutoDetection(bool enable)
{
    m_auto_detection_enabled = enable;
    if (enable) {
        std::cout << "[MP STUB] Auto-detection enabled - will switch between GPIO and Panel-MP modes" << std::endl;
    } else {
        std::cout << "[MP STUB] Auto-detection disabled" << std::endl;
    }
}

void MS2000MPStub::onGPIOActivity()
{
    if (!m_auto_detection_enabled) return;

    m_last_activity_time = m_cycle_counter;
    m_gpio_activity_count++;

    // If we see GPIO activity and we're in Panel-MP mode, switch back
    if (!m_gpio_mode_active && m_gpio_activity_count >= 2) {
        switchToGPIOMode();
    }
}

void MS2000MPStub::switchToPanelMPMode()
{
    if (m_gpio_mode_active) {
        m_gpio_mode_active = false;
        m_sci_activity_count = 0;
        std::cout << "[MP STUB] 🔄 Switched to Panel-MP mode (SCI communication detected)" << std::endl;
        std::cout << "[MP STUB] GPIO LCD adapter will be disabled" << std::endl;
    }
}

void MS2000MPStub::switchToGPIOMode()
{
    if (!m_gpio_mode_active) {
        m_gpio_mode_active = true;
        m_gpio_activity_count = 0;
        std::cout << "[MP STUB] 🔄 Switched to GPIO mode (GPIO activity detected)" << std::endl;
        std::cout << "[MP STUB] Panel-MP SCI communication will be disabled" << std::endl;
    }
}

// ==== RTC Implementation ====

void MS2000MPStub::updateRTC(uint32_t rtc_seconds)
{
    // Convert Unix timestamp to BCD components
    // rtc_seconds is seconds since 2000-01-01 00:00:00
    
    uint32_t total_seconds = rtc_seconds;
    uint32_t seconds = total_seconds % 60;
    total_seconds /= 60;
    uint32_t minutes = total_seconds % 60;
    total_seconds /= 60;
    uint32_t hours = total_seconds % 24;
    total_seconds /= 24;
    
    // Simple day count (not full calendar, but functional)
    // For full calendar we'd need proper date math
    uint32_t days = total_seconds;
    
    // Approximate year/month/day from day count
    // Using 2000 as epoch (leap year)
    uint32_t year = 0; // years since 2000
    uint32_t month = 1;
    uint32_t day = 1;
    uint32_t wday = 6; // 2000-01-01 was Saturday
    
    // Simple approximation for embedding
    if (days > 0) {
        year = days / 365;
        uint32_t day_of_year = days % 365;
        month = (day_of_year / 30) + 1;
        day = (day_of_year % 30) + 1;
        wday = (6 + days) % 7; // 2000-01-01 was Saturday
    }
    
    // Clamp values
    if (month > 12) month = 12;
    if (day > 31) day = 31;
    
    m_registers.rtc_sec = static_cast<uint8_t>(seconds);
    m_registers.rtc_min = static_cast<uint8_t>(minutes);
    m_registers.rtc_hour = static_cast<uint8_t>(hours);
    m_registers.rtc_day = static_cast<uint8_t>(day);
    m_registers.rtc_month = static_cast<uint8_t>(month);
    m_registers.rtc_year = static_cast<uint8_t>(year & 0x7F);
    m_registers.rtc_wday = static_cast<uint8_t>(wday);
    
    // RTC enabled bit
    m_registers.rtc_ctrl |= 0x01;
}

void MS2000MPStub::loadRTCFromNVRAM(const std::vector<uint8_t>& nvram)
{
    // RTC stored at offset 0x1000 in NVRAM (8 bytes)
    // [sec, min, hour, wday, day, month, year, ctrl]
    if (nvram.size() >= 0x1008) {
        const uint8_t* rtc_data = &nvram[0x1000];
        m_registers.rtc_sec = rtc_data[0];
        m_registers.rtc_min = rtc_data[1];
        m_registers.rtc_hour = rtc_data[2];
        m_registers.rtc_wday = rtc_data[3];
        m_registers.rtc_day = rtc_data[4];
        m_registers.rtc_month = rtc_data[5];
        m_registers.rtc_year = rtc_data[6];
        m_registers.rtc_ctrl = rtc_data[7];
        std::cout << "[MP STUB] RTC loaded from NVRAM: 20" << (int)m_registers.rtc_year 
                  << "-" << (int)m_registers.rtc_month << "-" << (int)m_registers.rtc_day
                  << " " << (int)m_registers.rtc_hour << ":" << (int)m_registers.rtc_min << ":" << (int)m_registers.rtc_sec << std::endl;
    }
}

void MS2000MPStub::saveRTCToNVRAM(std::vector<uint8_t>& nvram)
{
    // Ensure NVRAM is large enough
    if (nvram.size() < 0x1008) {
        nvram.resize(0x1008, 0);
    }
    
    // Store RTC at offset 0x1000
    uint8_t* rtc_data = &nvram[0x1000];
    rtc_data[0] = m_registers.rtc_sec;
    rtc_data[1] = m_registers.rtc_min;
    rtc_data[2] = m_registers.rtc_hour;
    rtc_data[3] = m_registers.rtc_wday;
    rtc_data[4] = m_registers.rtc_day;
    rtc_data[5] = m_registers.rtc_month;
    rtc_data[6] = m_registers.rtc_year;
    rtc_data[7] = m_registers.rtc_ctrl;
}

uint32_t MS2000MPStub::getRTCTime() const
{
    // Convert RTC registers back to Unix timestamp (seconds since 2000-01-01)
    // Approximate calculation
    uint32_t year = m_registers.rtc_year;
    uint32_t month = m_registers.rtc_month;
    uint32_t day = m_registers.rtc_day;
    
    // Simple day count approximation
    uint32_t days = year * 365 + (month - 1) * 30 + (day - 1);
    
    return days * 86400 + m_registers.rtc_hour * 3600 + m_registers.rtc_min * 60 + m_registers.rtc_sec;
}

void MS2000MPStub::setRTCTime(uint32_t unix_time)
{
    updateRTC(unix_time);
}

// Add RTC address decoding
bool MS2000MPStub::isRTCAddress(uint32_t address) const
{
    return (address & 0xFF) >= 0x68 && (address & 0xFF) <= 0x6F;
}

} // namespace MS2000
