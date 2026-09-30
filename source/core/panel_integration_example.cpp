#include "panel_if_adapter.h"
#include "h8s2350_v2_1.h"
#include <iostream>

using namespace MS2000;

// Example: Your existing panel system
class MyExistingPanel {
public:
    // Your existing panel methods
    uint8_t readButtonColumns() { return 0xFF; }  // Your button reading
    void setRowOutput(uint8_t row) { /* Your row setting */ }
    void setLEDRow(uint8_t row) { /* Your LED row setting */ }
    void setLEDColumns(uint8_t mask) { /* Your LED column setting */ }
    void setMuxChannel(uint8_t ch) { /* Your ADC mux setting */ }
    uint16_t readADCValue() { return 512; }  // Your ADC reading
};

// Example: Adapter for your existing panel
class MyPanelAdapter : public GenericPanelIO {
public:
    explicit MyPanelAdapter(MyExistingPanel& panel) : m_panel(panel) {}
    
    // Implement the GenericPanelIO interface
    uint8_t readButtonMatrix() override {
        return m_panel.readButtonColumns();
    }
    
    void setButtonRow(uint8_t row) override {
        m_panel.setRowOutput(row);
    }
    
    void setLEDRowSelect(uint8_t row) override {
        m_panel.setLEDRow(row);
    }
    
    void setLEDColumnMask(uint8_t mask) override {
        m_panel.setLEDColumns(mask);
    }
    
    void setADCMuxChannel(uint8_t channel) override {
        m_panel.setMuxChannel(channel);
    }
    
    uint16_t readADC() override {
        return m_panel.readADCValue();
    }

private:
    MyExistingPanel& m_panel;
};

// Example: How to use the integration
void exampleIntegration() {
    std::cout << "=== MS-2000 Panel Integration Example ===" << std::endl;
    
    // 1. Create your existing panel
    MyExistingPanel myPanel;
    
    // 2. Create adapter for your panel
    MyPanelAdapter panelAdapter(myPanel);
    
    // 3. Create PanelIF adapter
    PanelIFAdapter panelIF(panelAdapter);
    
    // 4. Create bus and peripherals
    H8SBus bus;
    Peripherals periph(bus, panelIF);
    
    std::cout << "Panel integration ready!" << std::endl;
    std::cout << "Your panel is now accessible via firmware addresses:" << std::endl;
    std::cout << "  - Buttons: Read from 0x" << std::hex << MS2K_PORT_COL_ADDR << std::endl;
    std::cout << "  - LEDs: Write to 0x" << std::hex << MS2K_LED_ROW_ADDR << " and 0x" << MS2K_LED_COL_ADDR << std::endl;
    std::cout << "  - ADC: Read from 0x" << std::hex << MS2K_ADDA_H_ADDR << "-0x" << MS2K_ADDA_L_ADDR << std::endl;
    std::cout << std::dec << std::endl;
    
    // 5. Example: CPU can now access your panel through firmware addresses
    // This simulates what the MS-2000 firmware would do:
    
    // Read button state
    bus.write8(MS2K_PORT_ROW_ADDR, 0);  // Select row 0
    uint8_t buttonData = bus.read8(MS2K_PORT_COL_ADDR);  // Read columns
    std::cout << "Button data from row 0: 0x" << std::hex << (int)buttonData << std::endl;
    
    // Set LED pattern
    bus.write8(MS2K_LED_ROW_ADDR, 1);   // Select LED row 1
    bus.write8(MS2K_LED_COL_ADDR, 0xAA); // Set alternating pattern
    std::cout << "LED row 1 set to alternating pattern" << std::endl;
    
    // Read ADC
    bus.write8(MS2K_ADCSR_ADDR, 0);     // Select ADC channel 0
    uint8_t adcHigh = bus.read8(MS2K_ADDA_H_ADDR);
    uint8_t adcLow = bus.read8(MS2K_ADDA_L_ADDR);
    uint16_t adcValue = (adcHigh << 8) | adcLow;
    std::cout << "ADC channel 0 value: " << std::dec << adcValue << std::endl;
    
    std::cout << "Integration example completed!" << std::endl;
}

// Example: How to use with a real CPU emulator
void exampleWithCPU() {
    std::cout << "\n=== Example with CPU Emulator ===" << std::endl;
    
    // Create panel and adapters
    MyExistingPanel myPanel;
    MyPanelAdapter panelAdapter(myPanel);
    PanelIFAdapter panelIF(panelAdapter);
    
    // Create bus and peripherals
    H8SBus bus;
    Peripherals periph(bus, panelIF);
    
    // Your CPU emulator would now:
    // 1. Execute instructions that read/write to firmware addresses
    // 2. The bus routes these to the peripherals
    // 3. The peripherals call the PanelIF adapter
    // 4. The adapter calls your existing panel methods
    
    std::cout << "CPU emulator can now access panel through firmware addresses!" << std::endl;
    std::cout << "Example CPU instructions:" << std::endl;
    std::cout << "  MOV.B @0xFF50, R0  ; Read button columns" << std::endl;
    std::cout << "  MOV.B R0, @0xFF53  ; Set LED row" << std::endl;
    std::cout << "  MOV.B R1, @0xFF54  ; Set LED columns" << std::endl;
    std::cout << "  MOV.B R2, @0xFF98  ; Set ADC channel" << std::endl;
    std::cout << "  MOV.B @0xFF90, R3  ; Read ADC high byte" << std::endl;
    std::cout << "  MOV.B @0xFF91, R4  ; Read ADC low byte" << std::endl;
}

int main() {
    exampleIntegration();
    exampleWithCPU();
    return 0;
}
