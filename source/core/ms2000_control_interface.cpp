#include "ms2000_control_interface.h"
#include "h8s2350_emulator.h"
#include "dsp56362_emulator.h"
#include "lcd_display.h"
#include "ms2000_hardware_mapping.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace MS2000;

// MS2000ControlInterface Implementation
MS2000ControlInterface::MS2000ControlInterface()
    : m_h8s(nullptr)
    , m_dsp(nullptr)
    , m_initialized(false)
    , m_updateRate(60.0f)
    , m_running(false)
    , m_lastUpdate(std::chrono::steady_clock::now()) {
    
    // Initialize hardware state
    m_hardwareState = HardwareState();
    
    // Initialize knob values
    m_knobValues.fill(0.0f);
    
    // Initialize matrices
    m_buttonMatrix.fill(false);
    m_ledMatrix.fill(false);
    m_buttonMatrixIO.fill(0);
    m_ledMatrixIO.fill(0);
    
    // Initialize LCD
    // LCD is already initialized in constructor
}

MS2000ControlInterface::~MS2000ControlInterface() {
    shutdown();
}

bool MS2000ControlInterface::initialize(H8S2350Emulator* h8s, DSP56362Emulator* dsp) {
    if (m_initialized) {
        return true;
    }
    
    m_h8s = h8s;
    m_dsp = dsp;
    
    if (!m_h8s || !m_dsp) {
        std::cerr << "MS2000ControlInterface: Invalid H8S or DSP emulator" << std::endl;
        return false;
    }
    
    // Initialize default MS2000 controls using real hardware mapping
    initializeDefaultControls();
    setupHardwareMappings();
    
    m_initialized = true;
    m_running = true;
    m_lastUpdate = std::chrono::steady_clock::now();
    
    std::cout << "MS2000ControlInterface: Initialized successfully with real hardware mapping" << std::endl;
    return true;
}

void MS2000ControlInterface::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    m_running = false;
    m_initialized = false;
    
    std::cout << "MS2000ControlInterface: Shutdown complete" << std::endl;
}

void MS2000ControlInterface::setHardwareState(const HardwareState& state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hardwareState = state;
}

void MS2000ControlInterface::initializeDefaultControls() {
    // Initialize all knob values to 0.5 (middle position)
    for (size_t i = 0; i < m_knobValues.size(); ++i) {
        m_knobValues[i] = 0.5f;
    }
    
    // Initialize LCD with default text
    m_lcd.clear();
    m_lcd.setText(0, "MS2000 Emulator");
    m_lcd.setText(1, "Ready");
    
    std::cout << "MS2000ControlInterface: Default controls initialized" << std::endl;
}

void MS2000ControlInterface::setupHardwareMappings() {
    // Set up initial hardware state
    m_hardwareState.timbre = TimbreSelect::TIMBRE_1;
    m_hardwareState.fx = FXMode::MOD;
    m_hardwareState.lfo = LFOSelect::LFO_1;
    m_hardwareState.seq = SeqMode::SEQ_OFF;
    m_hardwareState.lane = SeqLane::LANE_LEVEL;
    
    std::cout << "MS2000ControlInterface: Hardware mappings configured" << std::endl;
}

// Knob Management (using real ADC bank/mux mapping)
void MS2000ControlInterface::setKnobValue(ADCBank bank, ADCMux mux, float value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Clamp value to 0.0-1.0 range
    value = std::max(0.0f, std::min(1.0f, value));
    
    // Convert bank/mux to knob index
    uint8_t knobIndex = adcToKnobIndex(bank, mux);
    if (knobIndex < m_knobValues.size()) {
        m_knobValues[knobIndex] = value;
        
        // Get knob name and role for debugging
        const KnobMapping* mapping = getKnobMapping(bank, mux);
        if (mapping) {
            std::string role = getKnobRole(bank, mux);
            std::cout << "Knob " << mapping->name << " (" << role << ") = " << value << std::endl;
        }
    }
}

float MS2000ControlInterface::getKnobValue(ADCBank bank, ADCMux mux) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint8_t knobIndex = adcToKnobIndex(bank, mux);
    if (knobIndex < m_knobValues.size()) {
        return m_knobValues[knobIndex];
    }
    return 0.0f;
}

void MS2000ControlInterface::setKnobValue(uint8_t knobIndex, float value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (knobIndex < m_knobValues.size()) {
        m_knobValues[knobIndex] = std::max(0.0f, std::min(1.0f, value));
    }
}

float MS2000ControlInterface::getKnobValue(uint8_t knobIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (knobIndex < m_knobValues.size()) {
        return m_knobValues[knobIndex];
    }
    return 0.0f;
}

std::string MS2000ControlInterface::getKnobName(ADCBank bank, ADCMux mux) const {
    const KnobMapping* mapping = getKnobMapping(bank, mux);
    return mapping ? mapping->name : "Unknown";
}

std::string MS2000ControlInterface::getKnobRole(ADCBank bank, ADCMux mux) const {
    const KnobMapping* mapping = getKnobMapping(bank, mux);
    if (mapping) {
        return MS2000::getKnobRole(*mapping, m_hardwareState);
    }
    return "Unknown";
}

// Button Management (using real matrix mapping)
void MS2000ControlInterface::pressButton(uint8_t row, uint8_t col) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint8_t index = matrixIndex(row, col);
    if (index < m_buttonMatrix.size()) {
        m_buttonMatrix[index] = true;
        handleButtonPress(row, col);
    }
}

void MS2000ControlInterface::releaseButton(uint8_t row, uint8_t col) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint8_t index = matrixIndex(row, col);
    if (index < m_buttonMatrix.size()) {
        m_buttonMatrix[index] = false;
        handleButtonRelease(row, col);
    }
}

bool MS2000ControlInterface::isButtonPressed(uint8_t row, uint8_t col) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint8_t index = matrixIndex(row, col);
    if (index < m_buttonMatrix.size()) {
        return m_buttonMatrix[index];
    }
    return false;
}

std::string MS2000ControlInterface::getButtonName(uint8_t row, uint8_t col) const {
    const ButtonMapping* mapping = getButtonMapping(row, col);
    return mapping ? mapping->name : "Unknown";
}

// LED Management (using real matrix mapping)
void MS2000ControlInterface::setLED(uint8_t row, uint8_t col, bool lit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint8_t index = matrixIndex(row, col);
    if (index < m_ledMatrix.size()) {
        m_ledMatrix[index] = lit;
    }
}

void MS2000ControlInterface::setLEDBlinking(uint8_t row, uint8_t col, bool blinking, float rate) {
    // TODO: Implement LED blinking functionality
    (void)blinking;
    (void)rate;
}

bool MS2000ControlInterface::isLEDLit(uint8_t row, uint8_t col) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint8_t index = matrixIndex(row, col);
    if (index < m_ledMatrix.size()) {
        return m_ledMatrix[index];
    }
    return false;
}

std::string MS2000ControlInterface::getLEDName(uint8_t row, uint8_t col) const {
    const LEDMapping* mapping = getLEDMapping(row, col);
    return mapping ? mapping->name : "Unknown";
}

// Matrix Operations
void MS2000ControlInterface::setMatrixRow(uint8_t row, uint8_t value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (row < 8) {
        m_buttonMatrixIO[row] = value;
    }
}

uint8_t MS2000ControlInterface::getMatrixRow(uint8_t row) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (row < 8) {
        return m_buttonMatrixIO[row];
    }
    return 0;
}

void MS2000ControlInterface::setMatrixColumn(uint8_t col, uint8_t value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (col < 8) {
        // Update LED matrix column
        for (uint8_t row = 0; row < 8; ++row) {
            uint8_t index = matrixIndex(row, col);
            if (index < m_ledMatrix.size()) {
                m_ledMatrix[index] = (value & (1 << row)) != 0;
            }
        }
    }
}

uint8_t MS2000ControlInterface::getMatrixColumn(uint8_t col) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (col < 8) {
        uint8_t value = 0;
        for (uint8_t row = 0; row < 8; ++row) {
            uint8_t index = matrixIndex(row, col);
            if (index < m_buttonMatrix.size() && m_buttonMatrix[index]) {
                value |= (1 << row);
            }
        }
        return value;
    }
    return 0;
}

// Hardware Integration
void MS2000ControlInterface::updateADCValues() {
    // Update A/D converter values in H8S emulator
    if (m_h8s) {
        for (uint8_t bank = 4; bank <= 7; ++bank) {
            for (uint8_t mux = 0; mux < 8; ++mux) {
                ADCBank adcBank = static_cast<ADCBank>(bank);
                ADCMux adcMux = static_cast<ADCMux>(mux);
                
                float value = getKnobValue(adcBank, adcMux);
                uint16_t adcValue = static_cast<uint16_t>(value * 1023.0f); // 10-bit ADC
                
                // TODO: Update H8S A/D converter registers
                // m_h8s->setADCValue(bank, mux, adcValue);
            }
        }
    }
}

void MS2000ControlInterface::updateButtonMatrix() {
    // Update button matrix scanning in H8S emulator
    if (m_h8s) {
        for (uint8_t row = 0; row < 8; ++row) {
            uint8_t rowValue = 0;
            for (uint8_t col = 0; col < 8; ++col) {
                if (isButtonPressed(row, col)) {
                    rowValue |= (1 << col);
                }
            }
            setMatrixRow(row, rowValue);
            
            // TODO: Update H8S I/O port registers
            // m_h8s->setPortValue(row, rowValue);
        }
    }
}

void MS2000ControlInterface::updateLEDMatrix() {
    // Update LED matrix output in H8S emulator
    if (m_h8s) {
        for (uint8_t col = 0; col < 8; ++col) {
            uint8_t colValue = 0;
            for (uint8_t row = 0; row < 8; ++row) {
                if (isLEDLit(row, col)) {
                    colValue |= (1 << row);
                }
            }
            setMatrixColumn(col, colValue);
            
            // TODO: Update H8S I/O port registers
            // m_h8s->setPortValue(col + 8, colValue);
        }
    }
}

void MS2000ControlInterface::updateLCD() {
    // Update LCD display in H8S emulator
    if (m_h8s) {
        // TODO: Update H8S LCD interface registers
        // m_h8s->setLCDData(m_lcd.getText(0), m_lcd.getText(1));
    }
}

// Update and Processing
void MS2000ControlInterface::update() {
    if (!m_initialized || !m_running) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastUpdate);
    
    if (elapsed.count() >= (1000.0f / m_updateRate)) {
        processInputs();
        updateDisplay();
        m_lastUpdate = now;
    }
}

void MS2000ControlInterface::processInputs() {
    // Process button inputs and update hardware state
    updateButtonMatrix();
}

void MS2000ControlInterface::updateDisplay() {
    // Update all display elements
    updateADCValues();
    updateLEDMatrix();
    updateLCD();
}

// Internal methods
void MS2000ControlInterface::handleButtonPress(uint8_t row, uint8_t col) {
    const ButtonMapping* mapping = getButtonMapping(row, col);
    if (!mapping) {
        return;
    }
    
    std::cout << "Button pressed: " << mapping->name << std::endl;
    
    // Handle different button types
    switch (mapping->type) {
        case ButtonType::MOMENTARY:
            // Handle momentary button press
            break;
        case ButtonType::TOGGLE:
            // Handle toggle button press
            break;
        case ButtonType::LATCH2:
            // Handle 2-state latch button press
            break;
        case ButtonType::LATCH3:
            // Handle 3-state latch button press
            break;
    }
    
    // Handle specific button groups
    if (mapping->group == "timbre") {
        m_hardwareState.timbre = (m_hardwareState.timbre == TimbreSelect::TIMBRE_1) 
            ? TimbreSelect::TIMBRE_2 : TimbreSelect::TIMBRE_1;
    } else if (mapping->group == "fx") {
        m_hardwareState.fx = (m_hardwareState.fx == FXMode::MOD) 
            ? FXMode::DELAY : FXMode::MOD;
    } else if (mapping->group == "lfo") {
        m_hardwareState.lfo = (m_hardwareState.lfo == LFOSelect::LFO_1) 
            ? LFOSelect::LFO_2 : LFOSelect::LFO_1;
    } else if (mapping->group == "seq") {
        if (mapping->key == "SEQ_ENABLE") {
            m_hardwareState.seq = (m_hardwareState.seq == SeqMode::SEQ_OFF) 
                ? SeqMode::SEQ_ON : SeqMode::SEQ_OFF;
        } else if (mapping->key == "SEQ_EDIT") {
            // Cycle through sequencer lanes
            switch (m_hardwareState.lane) {
                case SeqLane::LANE_LEVEL: m_hardwareState.lane = SeqLane::LANE_PAN; break;
                case SeqLane::LANE_PAN: m_hardwareState.lane = SeqLane::LANE_PITCH; break;
                case SeqLane::LANE_PITCH: m_hardwareState.lane = SeqLane::LANE_LEVEL; break;
            }
        }
    }
}

void MS2000ControlInterface::handleButtonRelease(uint8_t row, uint8_t col) {
    const ButtonMapping* mapping = getButtonMapping(row, col);
    if (mapping) {
        std::cout << "Button released: " << mapping->name << std::endl;
    }
}

uint8_t MS2000ControlInterface::matrixIndex(uint8_t row, uint8_t col) const {
    return row * 8 + col;
}
