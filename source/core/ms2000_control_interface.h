#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <array>
#include "lcd_display.h"
#include "ms2000_hardware_mapping.h"

namespace MS2000 {

// Forward declarations
class H8S2350Emulator;
class DSP56362Emulator;

// Knob/Potentiometer emulation
struct Knob {
    uint8_t id;
    std::string name;
    float value;           // 0.0 to 1.0
    float minValue;
    float maxValue;
    bool isEnabled;
    std::function<void(float)> onChangeCallback;
    
    Knob(uint8_t knobId, const std::string& knobName, float min = 0.0f, float max = 1.0f)
        : id(knobId), name(knobName), value(0.0f), minValue(min), maxValue(max), isEnabled(true) {}
};

// Button emulation
struct Button {
    uint8_t id;
    std::string name;
    bool isPressed;
    bool isEnabled;
    std::function<void(bool)> onPressCallback;
    std::function<void(bool)> onReleaseCallback;
    
    Button(uint8_t buttonId, const std::string& buttonName)
        : id(buttonId), name(buttonName), isPressed(false), isEnabled(true) {}
};

// LED emulation
struct LED {
    uint8_t id;
    std::string name;
    bool isLit;
    bool isBlinking;
    float blinkRate;  // Hz
    
    LED(uint8_t ledId, const std::string& ledName)
        : id(ledId), name(ledName), isLit(false), isBlinking(false), blinkRate(1.0f) {}
};

// MS2000 Control Interface
class MS2000ControlInterface {
public:
    MS2000ControlInterface();
    ~MS2000ControlInterface();
    
    // Initialization
    bool initialize(H8S2350Emulator* h8s, DSP56362Emulator* dsp);
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Hardware State Management
    void setHardwareState(const HardwareState& state);
    HardwareState getHardwareState() const { return m_hardwareState; }
    
    // Knob Management (using real ADC bank/mux mapping)
    void setKnobValue(ADCBank bank, ADCMux mux, float value);
    float getKnobValue(ADCBank bank, ADCMux mux) const;
    void setKnobValue(uint8_t knobIndex, float value);
    float getKnobValue(uint8_t knobIndex) const;
    std::string getKnobName(ADCBank bank, ADCMux mux) const;
    std::string getKnobRole(ADCBank bank, ADCMux mux) const;
    
    // Button Management (using real matrix mapping)
    void pressButton(uint8_t row, uint8_t col);
    void releaseButton(uint8_t row, uint8_t col);
    bool isButtonPressed(uint8_t row, uint8_t col) const;
    std::string getButtonName(uint8_t row, uint8_t col) const;
    
    // LED Management (using real matrix mapping)
    void setLED(uint8_t row, uint8_t col, bool lit);
    void setLEDBlinking(uint8_t row, uint8_t col, bool blinking, float rate = 1.0f);
    bool isLEDLit(uint8_t row, uint8_t col) const;
    std::string getLEDName(uint8_t row, uint8_t col) const;
    
    // LCD Management
    LCDDisplay* getLCD() { return &m_lcd; }
    const LCDDisplay* getLCD() const { return &m_lcd; }
    
    // Matrix Operations (for 8x8 button/LED matrix)
    void setMatrixRow(uint8_t row, uint8_t value);
    uint8_t getMatrixRow(uint8_t row) const;
    void setMatrixColumn(uint8_t col, uint8_t value);
    uint8_t getMatrixColumn(uint8_t col) const;
    
    // Hardware Integration
    void updateADCValues();  // Update A/D converter values
    void updateButtonMatrix(); // Update button matrix scanning
    void updateLEDMatrix();   // Update LED matrix output
    void updateLCD();         // Update LCD display
    
    // Update and Processing
    void update();
    void processInputs();
    void updateDisplay();
    
private:
    // Core components
    H8S2350Emulator* m_h8s;
    DSP56362Emulator* m_dsp;
    bool m_initialized;
    
    // Hardware state
    HardwareState m_hardwareState;
    
    // Control elements (using real hardware mapping)
    std::array<float, 35> m_knobValues;  // 35 knobs total (including analog-only and multifunction knobs)
    std::array<bool, 64> m_buttonMatrix;  // 8x8 matrix = 64 buttons
    std::array<bool, 64> m_ledMatrix;     // 8x8 matrix = 64 LEDs (24 used)
    LCDDisplay m_lcd;
    
    // Matrix data for I/O operations
    std::array<uint8_t, 8> m_buttonMatrixIO;
    std::array<uint8_t, 8> m_ledMatrixIO;
    
    // Timing
    std::chrono::steady_clock::time_point m_lastUpdate;
    float m_updateRate;  // Hz
    
    // Threading
    mutable std::mutex m_mutex;
    std::atomic<bool> m_running;
    
    // Internal methods
    void initializeDefaultControls();
    void setupHardwareMappings();
    void handleButtonPress(uint8_t row, uint8_t col);
    void handleButtonRelease(uint8_t row, uint8_t col);
    uint8_t matrixIndex(uint8_t row, uint8_t col) const;
};

} // namespace MS2000
