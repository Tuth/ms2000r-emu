#pragma once
#include "h8s2350_v2_1.h"
#include <cstdint>
#include <functional>
#include <array>
#include <cstdio>

namespace MS2000 {

// ------------------------------
// Generic Panel Interface (to be implemented by your panel system)
// ------------------------------
struct GenericPanelIO {
    virtual ~GenericPanelIO() = default;
    
    // Button matrix scanning
    virtual uint8_t readButtonMatrix() = 0;  // Returns column data for current row
    
    // LED matrix control
    virtual void setLEDRowSelect(uint8_t row) = 0;
    virtual void setLEDColumnMask(uint8_t mask) = 0;
    
    // ADC/potentiometer reading
    virtual void setADCMuxChannel(uint8_t channel) = 0;  // 0-7 for 4051 multiplexer
    virtual uint16_t readADC() = 0;  // Returns 10-bit ADC value (0-1023)
    
    // Optional: Row driving for button matrix
    virtual void setButtonRow(uint8_t row) = 0;  // Set which row to scan
};

// ------------------------------
// PanelIF Adapter Implementation
// ------------------------------
class PanelIFAdapter : public PanelIF {
public:
    explicit PanelIFAdapter(GenericPanelIO& panel) : m_panel(panel) {}
    
    // PanelIF interface implementation
    uint8_t readColPort() override {
        return m_panel.readButtonMatrix();
    }
    
    void writeRowPort(uint8_t v) override {
        m_panel.setButtonRow(v);
    }
    
    void writeLedRowSel(uint8_t v) override {
        m_panel.setLEDRowSelect(v);
    }
    
    void writeLedCols(uint8_t v) override {
        m_panel.setLEDColumnMask(v);
    }
    
    void adcSetMux(uint8_t mux) override {
        m_panel.setADCMuxChannel(mux);
    }
    
    uint16_t adcRead() override {
        return m_panel.readADC();
    }

private:
    GenericPanelIO& m_panel;
};

// ------------------------------
// Simple Test Panel Implementation
// ------------------------------
class TestPanelIO : public GenericPanelIO {
public:
    TestPanelIO() {
        // Initialize test data
        m_buttonMatrix.fill(0xFF);  // All buttons released
        m_ledMatrix.fill(0x00);     // All LEDs off
        m_potValues.fill(0x200);    // Pots at middle position
    }
    
    // Button matrix simulation
    uint8_t readButtonMatrix() override {
        return m_buttonMatrix[m_currentRow];
    }
    
    void setButtonRow(uint8_t row) override {
        m_currentRow = row & 0x07;  // 8 rows max
    }
    
    // LED matrix simulation
    void setLEDRowSelect(uint8_t row) override {
        m_currentLedRow = row & 0x07;
    }
    
    void setLEDColumnMask(uint8_t mask) override {
        m_ledMatrix[m_currentLedRow] = mask;
    }
    
    // ADC simulation
    void setADCMuxChannel(uint8_t channel) override {
        m_currentAdcChannel = channel & 0x1F; // Support 0-31 (32 potentiometers)
    }
    
    uint16_t readADC() override {
        if (m_currentAdcChannel < 32) {
            return m_potValues[m_currentAdcChannel];
        }
        return 0;
    }
    
    // Test interface methods
    void setButton(uint8_t row, uint8_t col, bool pressed) {
        if (row < 8 && col < 8) {
            if (pressed) {
                m_buttonMatrix[row] &= ~(1 << col);  // Active low
            } else {
                m_buttonMatrix[row] |= (1 << col);
            }
        }
    }
    
    void setPotentiometer(uint8_t channel, uint16_t value) {
        if (channel < 32) {
            m_potValues[channel] = value & 0x3FF;  // 10-bit
        }
    }
    
    bool getLED(uint8_t row, uint8_t col) const {
        if (row < 8 && col < 8) {
            return (m_ledMatrix[row] & (1 << col)) != 0;
        }
        return false;
    }
    
    void printStatus() const {
        printf("=== Test Panel Status ===\n");
        printf("Current Row: %d, LED Row: %d, ADC Channel: %d\n", 
               m_currentRow, m_currentLedRow, m_currentAdcChannel);
        
        printf("Button Matrix:\n");
        for (int row = 0; row < 8; row++) {
            printf("  Row %d: 0x%02X\n", row, m_buttonMatrix[row]);
        }
        
        printf("LED Matrix:\n");
        for (int row = 0; row < 8; row++) {
            printf("  Row %d: 0x%02X\n", row, m_ledMatrix[row]);
        }
        
        printf("Potentiometers:\n");
        for (int ch = 0; ch < 32; ch++) {
            printf("  Ch %d: %d\n", ch, m_potValues[ch]);
        }
    }

private:
    std::array<uint8_t, 8> m_buttonMatrix;   // 8x8 button matrix
    std::array<uint8_t, 8> m_ledMatrix;      // 8x8 LED matrix
    std::array<uint16_t, 32> m_potValues;    // 32 potentiometer values (MS-2000 has 32 pots)
    
    uint8_t m_currentRow = 0;
    uint8_t m_currentLedRow = 0;
    uint8_t m_currentAdcChannel = 0;
};

} // namespace MS2000
