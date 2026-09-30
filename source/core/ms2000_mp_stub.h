#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include "real_lcd_display.h"
#include "ms2000_knob_mapping.h"

namespace MS2000 {

// MS2000 MP (Multiplexer Panel) Stub Emulation
// Based on real hardware: CPU → MP → LCD architecture
// 
// The MP handles:
// - LCD control signals (HD44780 compatible)
// - Potentiometer multiplexing
// - Button matrix scanning
// - Address decoding for CPU I/O operations

class MS2000MPStub {
private:
    // LCD Control Interface (HD44780 compatible)
    struct LCDInterface {
        uint8_t data_bus;      // D0-D7 data lines
        bool rs;               // Register Select (0=Instruction, 1=Data)
        bool rw;               // Read/Write (0=Write, 1=Read)
        bool enable;           // Enable signal
        bool busy;             // Busy flag
        uint8_t cursor_position; // Current cursor position
        std::array<std::string, 2> display_lines; // LCD content
    };
    
    // Potentiometer Interface (ADC multiplexing)
    struct PotInterface {
        uint8_t mux_channel;   // Current multiplexer channel (0-31)
        uint16_t adc_value;    // Current ADC value (10-bit)
        std::array<uint16_t, 32> pot_values; // All potentiometer values
    };
    
    // Button Matrix Interface
    struct ButtonMatrix {
        uint8_t row_select;    // Current row being scanned
        uint8_t column_data;   // Column data for current row
        std::array<uint8_t, 8> button_states; // Button states per row
    };
    
    // MP Control Registers (CPU accessible)
    struct MPRegisters {
        uint8_t lcd_control;      // LCD Control Register (0xFF60)
        uint8_t lcd_data;         // LCD Data Register (0xFF61)
        uint8_t lcd_status;       // LCD Status Register (0xFF62)
        uint8_t adc_control;      // ADC Control Register (0xFF63)
        uint8_t adc_data_low;     // ADC Data Low (0xFF64)
        uint8_t adc_data_high;    // ADC Data High (0xFF65)
        uint8_t button_control;   // Button Matrix Control (0xFF66)
        uint8_t button_data;      // Button Matrix Data (0xFF67)
        // RTC Registers (Panel CPU 8051 - 32.768kHz crystal IC25-A)
        uint8_t rtc_sec;          // RTC Second Counter (0xFF68)
        uint8_t rtc_min;          // RTC Minute Counter (0xFF69)
        uint8_t rtc_hour;         // RTC Hour Counter (0xFF6A)
        uint8_t rtc_wday;         // RTC Day of Week (0xFF6B)
        uint8_t rtc_day;          // RTC Day Counter (0xFF6C)
        uint8_t rtc_month;        // RTC Month Counter (0xFF6D)
        uint8_t rtc_year;         // RTC Year Counter (0xFF6E)
        uint8_t rtc_ctrl;         // RTC Control Register (0xFF6F)
    };
    
    // Internal state
    LCDInterface m_lcd;
    PotInterface m_pots;
    ButtonMatrix m_buttons;
    MPRegisters m_registers;
    
    // External LCD display connection
    RealLCDDisplay* m_lcd_display;
    
    // Knob mapping system
    std::unique_ptr<MS2000KnobMapping> m_knob_mapping;
    
    // Timing and state
    uint32_t m_cycle_counter;
    bool m_lcd_busy;
    uint8_t m_lcd_command_buffer;
    uint8_t m_lcd_data_buffer;

    // SCI Communication Protocol
    enum class SciProtocolState { IDLE, CMD, DATA } m_sci_state;
    std::vector<uint8_t> m_rx_buffer;  // Bytes to send to CPU
    uint32_t m_last_activity_time;

    // Auto-detection between GPIO and Panel-MP paths
    bool m_auto_detection_enabled;
    bool m_gpio_mode_active;
    uint32_t m_sci_activity_count;
    uint32_t m_gpio_activity_count;
    
public:
    MS2000MPStub();
    ~MS2000MPStub() = default;
    
    // CPU Interface - these are called by the H8S emulator
    uint8_t readRegister(uint32_t address);
    void writeRegister(uint32_t address, uint8_t value);

    // SCI Communication Interface (CPU ↔ MP)
    void onCpuTxByte(uint8_t data);  // CPU sends byte to MP via SCI
    bool hasRxByte(uint8_t& data);   // MP has byte to send to CPU

    // LCD Display connection
    void setLCDDisplay(RealLCDDisplay* lcd_display);
    
    // Button matrix simulation
    void setButtonState(uint8_t row, uint8_t col, bool pressed);
    void setPotentiometerValue(uint8_t channel, uint16_t value);
    
    // Knob mapping interface
    void setKnobValue(uint8_t knob_id, uint8_t value);
    uint8_t getKnobValue(uint8_t knob_id) const;
    void setKnobMode(KnobMode mode);
    KnobMode getKnobMode() const;
    
    // RTC interface
    void updateRTC(uint32_t rtc_seconds);           // Update RTC from clock system
    void loadRTCFromNVRAM(const std::vector<uint8_t>& nvram);  // Load RTC from NVRAM
    void saveRTCToNVRAM(std::vector<uint8_t>& nvram);          // Save RTC to NVRAM
    
    // Internal processing
    void update(uint32_t cycles);
    
    // Debug and status
    bool isLCDBusy() const { return m_lcd_busy; }
    uint8_t getCurrentMuxChannel() const { return m_pots.mux_channel; }
    uint16_t getCurrentADCValue() const { return m_pots.adc_value; }
    
    // ADC interface for firmware simulation
    void setADCMuxChannel(uint8_t channel);
    uint16_t readADC() const;
    
    // RTC accessors
    uint32_t getRTCTime() const;  // Return Unix timestamp from RTC registers
    void setRTCTime(uint32_t unix_time);  // Set RTC from Unix timestamp
    
    // Auto-detection methods
    void enableAutoDetection(bool enable = true);
    void onGPIOActivity();
    void switchToPanelMPMode();
    void switchToGPIOMode();
    bool isPanelMPModeActive() const { return !m_gpio_mode_active; }
    bool isAutoDetectionEnabled() const { return m_auto_detection_enabled; }
    
private:
    // Internal LCD processing
    void processLCDCommand(uint8_t command);
    void processLCDData(uint8_t data);
    void updateLCDDisplay();
    
    // Internal ADC processing
    void updateADC();
    
    // Internal button matrix processing
    void updateButtonMatrix();
    
    // Address decoding
    bool isLCDAddress(uint32_t address) const;
    bool isADCAddress(uint32_t address) const;
    bool isButtonAddress(uint32_t address) const;
    bool isRTCAddress(uint32_t address) const;
};

} // namespace MS2000
