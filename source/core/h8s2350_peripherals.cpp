#include "h8s2350_peripherals.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace MS2000 {

// TPU Mode Constants
namespace TPUMode {
    constexpr uint8_t DISABLED = 0x00;
    constexpr uint8_t INPUT_CAPTURE = 0x01;
    constexpr uint8_t OUTPUT_COMPARE = 0x02;
    constexpr uint8_t PWM = 0x03;
    constexpr uint8_t PWM_COMPLEMENTARY = 0x04;
    constexpr uint8_t RESET_SYNC = 0x05;
    constexpr uint8_t BUFFER = 0x06;
}

// SCI Mode Constants
namespace SCIMode {
    constexpr uint8_t ASYNC = 0x00;
    constexpr uint8_t CLOCK_SYNC = 0x01;
    constexpr uint8_t CLOCK_ASYNC = 0x02;
}

// ADC Mode Constants
namespace ADCMode {
    constexpr uint8_t SINGLE = 0x00;
    constexpr uint8_t SCAN = 0x01;
    constexpr uint8_t CONTINUOUS = 0x02;
}

// Constructor
H8S2350PeripheralEmulator::H8S2350PeripheralEmulator()
    : m_clock_frequency(20000000)
    , m_cycle_count(0)
    , m_debug_enabled(false)
    , m_dac_output(0)
{
    // Initialize all arrays to zero
    m_tpu_registers.fill({});
    m_tpu_channels.fill({});
    m_sci_registers.fill({});
    m_sci_configs.fill({});
    m_adc_channels.fill({});
    m_gpio_ports.fill({});
    
    // Initialize matrices
    for (auto& row : m_led_matrix.matrix) {
        row.fill(false);
    }
    for (auto& row : m_button_matrix.matrix) {
        row.fill(false);
    }
}

// Destructor
H8S2350PeripheralEmulator::~H8S2350PeripheralEmulator() {
    logMessage("Peripheral Emulator destroyed");
}

// Initialization
bool H8S2350PeripheralEmulator::initialize(uint32_t clock_frequency) {
    m_clock_frequency = clock_frequency;
    
    logMessage("Initializing H8S2350 Peripheral Emulator...");
    logMessage("Clock frequency: " + std::to_string(m_clock_frequency) + " Hz");
    
    initializeTPU();
    initializeSCI();
    initializeADC();
    initializeDAC();
    initializeGPIO();
    initializeMatrices();
    
    logMessage("Peripheral Emulator initialized successfully");
    return true;
}

void H8S2350PeripheralEmulator::reset() {
    logMessage("Resetting Peripheral Emulator...");
    
    // Reset all registers
    m_tpu_registers.fill({});
    m_sci_registers.fill({});
    m_adc_registers = {};
    m_dac_registers = {};
    m_gpio_ports.fill({});
    
    // Reset all channels
    m_tpu_channels.fill({});
    m_sci_configs.fill({});
    m_adc_channels.fill({});
    
    // Reset matrices
    for (auto& row : m_led_matrix.matrix) {
        row.fill(false);
    }
    for (auto& row : m_button_matrix.matrix) {
        row.fill(false);
    }
    
    m_cycle_count = 0;
    m_dac_output = 0;
    
    logMessage("Peripheral Emulator reset complete");
}

// TPU Functions
uint16_t H8S2350PeripheralEmulator::readTPURegister(uint8_t unit, uint8_t reg) {
    if (unit >= 6) return 0;
    
    switch (reg) {
        case 0: return m_tpu_registers[unit].TCR;
        case 1: return m_tpu_registers[unit].TSR;
        case 2: return m_tpu_registers[unit].TCNT;
        case 3: return m_tpu_registers[unit].GR;
        case 4: return m_tpu_registers[unit].ICR;
        case 5: return m_tpu_registers[unit].OCR;
        case 6: return m_tpu_registers[unit].TSTR;
        case 7: return m_tpu_registers[unit].TSYR;
        default: return 0;
    }
}

void H8S2350PeripheralEmulator::writeTPURegister(uint8_t unit, uint8_t reg, uint16_t value) {
    if (unit >= 6) return;
    
    switch (reg) {
        case 0: m_tpu_registers[unit].TCR = value; break;
        case 1: m_tpu_registers[unit].TSR = value; break;
        case 2: m_tpu_registers[unit].TCNT = value; break;
        case 3: m_tpu_registers[unit].GR = value; break;
        case 4: m_tpu_registers[unit].ICR = value; break;
        case 5: m_tpu_registers[unit].OCR = value; break;
        case 6: m_tpu_registers[unit].TSTR = value; break;
        case 7: m_tpu_registers[unit].TSYR = value; break;
    }
    
    if (m_debug_enabled) {
        logMessage("TPU" + std::to_string(unit) + " Reg " + std::to_string(reg) + 
                  " = 0x" + std::to_string(value));
    }
}

void H8S2350PeripheralEmulator::updateTPU(uint32_t cycles) {
    for (uint8_t unit = 0; unit < 6; unit++) {
        for (uint8_t channel = 0; channel < 6; channel++) {
            updateTPUChannel(unit, channel, cycles);
        }
    }
}

// SCI Functions
uint8_t H8S2350PeripheralEmulator::readSCIRegister(uint8_t unit, uint8_t reg) {
    if (unit >= 2) return 0;
    
    switch (reg) {
        case 0: return m_sci_registers[unit].SMR;
        case 1: return m_sci_registers[unit].BRR;
        case 2: return m_sci_registers[unit].SCR;
        case 3: return m_sci_registers[unit].TDR;
        case 4: return m_sci_registers[unit].SSR;
        case 5: return m_sci_registers[unit].RDR;
        default: return 0;
    }
}

void H8S2350PeripheralEmulator::writeSCIRegister(uint8_t unit, uint8_t reg, uint8_t value) {
    if (unit >= 2) return;
    
    switch (reg) {
        case 0: m_sci_registers[unit].SMR = value; break;
        case 1: m_sci_registers[unit].BRR = value; break;
        case 2: m_sci_registers[unit].SCR = value; break;
        case 3: 
            m_sci_registers[unit].TDR = value;
            m_sci_configs[unit].tx_buffer.push(value);
            break;
        case 4: m_sci_registers[unit].SSR = value; break;
        case 5: m_sci_registers[unit].RDR = value; break;
    }
    
    if (m_debug_enabled) {
        logMessage("SCI" + std::to_string(unit) + " Reg " + std::to_string(reg) + 
                  " = 0x" + std::to_string(static_cast<int>(value)));
    }
}

void H8S2350PeripheralEmulator::updateSCI(uint32_t cycles) {
    for (uint8_t unit = 0; unit < 2; unit++) {
        updateSCIChannel(unit, cycles);
    }
}

void H8S2350PeripheralEmulator::setMIDICallbacks(std::function<void(uint8_t)> rx_callback, 
                                                std::function<void(uint8_t)> tx_callback) {
    m_midi_rx_callback = rx_callback;
    m_midi_tx_callback = tx_callback;
}

void H8S2350PeripheralEmulator::sendMIDIByte(uint8_t unit, uint8_t data) {
    if (unit >= 2) return;
    
    m_sci_configs[unit].rx_buffer.push(data);
    
    if (m_midi_rx_callback) {
        m_midi_rx_callback(data);
    }
    
    if (m_debug_enabled) {
        logMessage("MIDI RX: 0x" + std::to_string(static_cast<int>(data)));
    }
}

uint8_t H8S2350PeripheralEmulator::receiveMIDIByte(uint8_t unit) {
    if (unit >= 2) return 0;
    
    if (!m_sci_configs[unit].tx_buffer.empty()) {
        uint8_t data = m_sci_configs[unit].tx_buffer.front();
        m_sci_configs[unit].tx_buffer.pop();
        
        if (m_midi_tx_callback) {
            m_midi_tx_callback(data);
        }
        
        if (m_debug_enabled) {
            logMessage("MIDI TX: 0x" + std::to_string(static_cast<int>(data)));
        }
        
        return data;
    }
    
    return 0;
}

// ADC Functions
uint8_t H8S2350PeripheralEmulator::readADCRegister(uint8_t reg) {
    switch (reg) {
        case 0: return m_adc_registers.ADCSR;
        case 1: return m_adc_registers.ADCR;
        case 2: return m_adc_registers.ADDR;
        case 3: return m_adc_registers.ADDRH;
        default: return 0;
    }
}

void H8S2350PeripheralEmulator::writeADCRegister(uint8_t reg, uint8_t value) {
    switch (reg) {
        case 0: m_adc_registers.ADCSR = value; break;
        case 1: m_adc_registers.ADCR = value; break;
        case 2: m_adc_registers.ADDR = value; break;
        case 3: m_adc_registers.ADDRH = value; break;
    }
    
    if (m_debug_enabled) {
        logMessage("ADC Reg " + std::to_string(reg) + " = 0x" + std::to_string(static_cast<int>(value)));
    }
}

void H8S2350PeripheralEmulator::setADCValue(uint8_t channel, uint16_t value) {
    if (channel >= 8) return;
    
    m_adc_channels[channel].value = value & 0x3FF; // 10-bit value
    
    if (m_potentiometer_callback) {
        m_potentiometer_callback(channel, value);
    }
    
    if (m_debug_enabled) {
        logMessage("ADC Channel " + std::to_string(channel) + " = " + std::to_string(value));
    }
}

uint16_t H8S2350PeripheralEmulator::getADCValue(uint8_t channel) {
    if (channel >= 8) return 0;
    return m_adc_channels[channel].value;
}

void H8S2350PeripheralEmulator::startADCConversion(uint8_t channel) {
    if (channel >= 8) return;
    
    m_adc_channels[channel].conversion_complete = false;
    m_adc_channels[channel].interrupt_pending = false;
    
    // Simulate conversion time
    if (m_debug_enabled) {
        logMessage("Starting ADC conversion on channel " + std::to_string(channel));
    }
}

void H8S2350PeripheralEmulator::updateADC(uint32_t cycles) {
    for (uint8_t channel = 0; channel < 8; channel++) {
        updateADCChannel(channel, cycles);
    }
}

// DAC Functions
uint16_t H8S2350PeripheralEmulator::readDACRegister(uint8_t reg) {
    switch (reg) {
        case 0: return m_dac_registers.DADR;
        case 1: return m_dac_registers.DACR;
        default: return 0;
    }
}

void H8S2350PeripheralEmulator::writeDACRegister(uint8_t reg, uint16_t value) {
    switch (reg) {
        case 0: 
            m_dac_registers.DADR = value;
            m_dac_output = value;
            if (m_audio_output_callback) {
                m_audio_output_callback(value);
            }
            break;
        case 1: m_dac_registers.DACR = value & 0xFF; break;
    }
    
    if (m_debug_enabled) {
        logMessage("DAC Reg " + std::to_string(reg) + " = 0x" + std::to_string(value));
    }
}

void H8S2350PeripheralEmulator::setAudioOutputCallback(std::function<void(uint16_t)> callback) {
    m_audio_output_callback = callback;
}

// GPIO Functions
uint8_t H8S2350PeripheralEmulator::readGPIOPort(uint8_t port, uint8_t reg) {
    if (port >= 8) return 0;
    
    switch (reg) {
        case 0: return m_gpio_ports[port].DDR;
        case 1: return m_gpio_ports[port].DR;
        case 2: return m_gpio_ports[port].PDR;
        case 3: return m_gpio_ports[port].PCR;
        default: return 0;
    }
}

void H8S2350PeripheralEmulator::writeGPIOPort(uint8_t port, uint8_t reg, uint8_t value) {
    if (port >= 8) return;
    
    switch (reg) {
        case 0: m_gpio_ports[port].DDR = value; break;
        case 1: m_gpio_ports[port].DR = value; break;
        case 2: m_gpio_ports[port].PDR = value; break;
        case 3: m_gpio_ports[port].PCR = value; break;
    }
    
    if (m_debug_enabled) {
        logMessage("GPIO Port " + std::to_string(port) + " Reg " + std::to_string(reg) + 
                  " = 0x" + std::to_string(static_cast<int>(value)));
    }
}

void H8S2350PeripheralEmulator::setGPIOInput(uint8_t port, uint8_t pin, bool value) {
    if (port >= 8 || pin >= 8) return;
    
    // Update port data register
    if (value) {
        m_gpio_ports[port].PDR |= (1 << pin);
    } else {
        m_gpio_ports[port].PDR &= ~(1 << pin);
    }
}

bool H8S2350PeripheralEmulator::getGPIOOutput(uint8_t port, uint8_t pin) {
    if (port >= 8 || pin >= 8) return false;
    
    return (m_gpio_ports[port].DR & (1 << pin)) != 0;
}

// LED Matrix Functions
void H8S2350PeripheralEmulator::setLED(uint8_t row, uint8_t col, bool state) {
    if (row >= 8 || col >= 8) return;
    
    m_led_matrix.matrix[row][col] = state;
    
    if (m_debug_enabled) {
        logMessage("LED [" + std::to_string(row) + "," + std::to_string(col) + "] = " + 
                  (state ? "ON" : "OFF"));
    }
}

bool H8S2350PeripheralEmulator::getLED(uint8_t row, uint8_t col) {
    if (row >= 8 || col >= 8) return false;
    return m_led_matrix.matrix[row][col];
}

void H8S2350PeripheralEmulator::updateLEDMatrix() {
    // Update LED matrix based on GPIO port states
    // This would typically be driven by the CPU writing to specific I/O addresses
}

// Button Matrix Functions
void H8S2350PeripheralEmulator::setButton(uint8_t row, uint8_t col, bool pressed) {
    if (row >= 8 || col >= 8) return;
    
    m_button_matrix.matrix[row][col] = pressed;
    
    if (m_button_callback) {
        m_button_callback(row, col, pressed);
    }
    
    if (m_debug_enabled) {
        logMessage("Button [" + std::to_string(row) + "," + std::to_string(col) + "] = " + 
                  (pressed ? "PRESSED" : "RELEASED"));
    }
}

bool H8S2350PeripheralEmulator::getButton(uint8_t row, uint8_t col) {
    if (row >= 8 || col >= 8) return false;
    return m_button_matrix.matrix[row][col];
}

void H8S2350PeripheralEmulator::updateButtonMatrix() {
    // Update button matrix scanning
    // This would typically be driven by the CPU reading from specific I/O addresses
}

void H8S2350PeripheralEmulator::setButtonCallback(std::function<void(int, int, bool)> callback) {
    m_button_callback = callback;
}

// Potentiometer Functions
void H8S2350PeripheralEmulator::setPotentiometerValue(uint8_t channel, uint16_t value) {
    setADCValue(channel, value);
}

uint16_t H8S2350PeripheralEmulator::getPotentiometerValue(uint8_t channel) {
    return getADCValue(channel);
}

void H8S2350PeripheralEmulator::setPotentiometerCallback(std::function<void(int, uint16_t)> callback) {
    m_potentiometer_callback = callback;
}

// Main Update Function
void H8S2350PeripheralEmulator::update(uint32_t cycles) {
    m_cycle_count += cycles;
    
    updateTPU(cycles);
    updateSCI(cycles);
    updateADC(cycles);
    updateLEDMatrix();
    updateButtonMatrix();
}

// Debug Functions
void H8S2350PeripheralEmulator::dumpTPUStatus() {
    std::cout << "=== TPU Status ===" << std::endl;
    for (uint8_t unit = 0; unit < 6; unit++) {
        std::cout << "TPU" << static_cast<int>(unit) << ": ";
        std::cout << "TCR=0x" << std::hex << m_tpu_registers[unit].TCR;
        std::cout << " TCNT=0x" << std::hex << m_tpu_registers[unit].TCNT;
        std::cout << " TSTR=0x" << std::hex << m_tpu_registers[unit].TSTR << std::endl;
    }
}

void H8S2350PeripheralEmulator::dumpSCIStatus() {
    std::cout << "=== SCI Status ===" << std::endl;
    for (uint8_t unit = 0; unit < 2; unit++) {
        std::cout << "SCI" << static_cast<int>(unit) << ": ";
        std::cout << "SMR=0x" << std::hex << static_cast<int>(m_sci_registers[unit].SMR);
        std::cout << " BRR=0x" << std::hex << static_cast<int>(m_sci_registers[unit].BRR);
        std::cout << " SCR=0x" << std::hex << static_cast<int>(m_sci_registers[unit].SCR);
        std::cout << " SSR=0x" << std::hex << static_cast<int>(m_sci_registers[unit].SSR) << std::endl;
    }
}

void H8S2350PeripheralEmulator::dumpADCStatus() {
    std::cout << "=== ADC Status ===" << std::endl;
    std::cout << "ADCSR=0x" << std::hex << static_cast<int>(m_adc_registers.ADCSR);
    std::cout << " ADCR=0x" << std::hex << static_cast<int>(m_adc_registers.ADCR) << std::endl;
    
    for (uint8_t channel = 0; channel < 8; channel++) {
        std::cout << "AN" << static_cast<int>(channel) << "=" << std::dec 
                  << m_adc_channels[channel].value << " ";
    }
    std::cout << std::endl;
}

void H8S2350PeripheralEmulator::dumpDACStatus() {
    std::cout << "=== DAC Status ===" << std::endl;
    std::cout << "DADR=0x" << std::hex << m_dac_registers.DADR;
    std::cout << " DACR=0x" << std::hex << static_cast<int>(m_dac_registers.DACR);
    std::cout << " Output=" << std::dec << m_dac_output << std::endl;
}

void H8S2350PeripheralEmulator::dumpGPIOStatus() {
    std::cout << "=== GPIO Status ===" << std::endl;
    for (uint8_t port = 0; port < 8; port++) {
        std::cout << "Port" << static_cast<char>('A' + port) << ": ";
        std::cout << "DDR=0x" << std::hex << static_cast<int>(m_gpio_ports[port].DDR);
        std::cout << " DR=0x" << std::hex << static_cast<int>(m_gpio_ports[port].DR);
        std::cout << " PDR=0x" << std::hex << static_cast<int>(m_gpio_ports[port].PDR) << std::endl;
    }
}

void H8S2350PeripheralEmulator::dumpLEDMatrix() {
    std::cout << "=== LED Matrix ===" << std::endl;
    for (uint8_t row = 0; row < 8; row++) {
        std::cout << "Row " << static_cast<int>(row) << ": ";
        for (uint8_t col = 0; col < 8; col++) {
            std::cout << (m_led_matrix.matrix[row][col] ? "1" : "0") << " ";
        }
        std::cout << std::endl;
    }
}

void H8S2350PeripheralEmulator::dumpButtonMatrix() {
    std::cout << "=== Button Matrix ===" << std::endl;
    for (uint8_t row = 0; row < 8; row++) {
        std::cout << "Row " << static_cast<int>(row) << ": ";
        for (uint8_t col = 0; col < 8; col++) {
            std::cout << (m_button_matrix.matrix[row][col] ? "1" : "0") << " ";
        }
        std::cout << std::endl;
    }
}

// Private Helper Functions
void H8S2350PeripheralEmulator::initializeTPU() {
    logMessage("Initializing TPU units...");
    
    for (uint8_t unit = 0; unit < 6; unit++) {
        m_tpu_registers[unit] = {};
        for (uint8_t channel = 0; channel < 6; channel++) {
            uint8_t index = unit * 6 + channel;
            m_tpu_channels[index] = {};
            m_tpu_channels[index].enabled = false;
            m_tpu_channels[index].mode = TPUMode::DISABLED;
        }
    }
}

void H8S2350PeripheralEmulator::initializeSCI() {
    logMessage("Initializing SCI units...");
    
    for (uint8_t unit = 0; unit < 2; unit++) {
        m_sci_registers[unit] = {};
        m_sci_configs[unit] = {};
        m_sci_configs[unit].baud_rate = (unit == 0) ? 31250 : 9600; // MIDI vs Debug
        m_sci_configs[unit].data_bits = 8;
        m_sci_configs[unit].stop_bits = 1;
        m_sci_configs[unit].parity_enabled = false;
        m_sci_configs[unit].interrupt_enabled = false;
    }
}

void H8S2350PeripheralEmulator::initializeADC() {
    logMessage("Initializing ADC...");
    
    m_adc_registers = {};
    for (uint8_t channel = 0; channel < 8; channel++) {
        m_adc_channels[channel] = {};
        m_adc_channels[channel].enabled = false;
        m_adc_channels[channel].value = 512; // Middle value
        m_adc_channels[channel].conversion_complete = false;
        m_adc_channels[channel].interrupt_enabled = false;
        m_adc_channels[channel].interrupt_pending = false;
    }
}

void H8S2350PeripheralEmulator::initializeDAC() {
    logMessage("Initializing DAC...");
    
    m_dac_registers = {};
    m_dac_output = 0;
}

void H8S2350PeripheralEmulator::initializeGPIO() {
    logMessage("Initializing GPIO ports...");
    
    for (uint8_t port = 0; port < 8; port++) {
        m_gpio_ports[port] = {};
    }
}

void H8S2350PeripheralEmulator::initializeMatrices() {
    logMessage("Initializing LED and Button matrices...");
    
    m_led_matrix = {};
    m_button_matrix = {};
}

void H8S2350PeripheralEmulator::updateTPUChannel(uint8_t unit, uint8_t channel, uint32_t cycles) {
    uint8_t index = unit * 6 + channel;
    TPUChannel& tpu_channel = m_tpu_channels[index];
    
    if (!tpu_channel.enabled) return;
    
    // Update counter based on mode
    switch (tpu_channel.mode) {
        case TPUMode::OUTPUT_COMPARE:
        case TPUMode::PWM:
            // Increment counter
            m_tpu_registers[unit].TCNT += cycles;
            
            // Check for compare match
            if (m_tpu_registers[unit].TCNT >= tpu_channel.compare_value) {
                m_tpu_registers[unit].TCNT = 0; // Reset counter
                if (tpu_channel.interrupt_enabled) {
                    triggerTPUInterrupt(unit, channel);
                }
            }
            break;
            
        case TPUMode::INPUT_CAPTURE:
            // Input capture would be triggered by external events
            break;
    }
}

void H8S2350PeripheralEmulator::updateSCIChannel(uint8_t unit, uint32_t cycles) {
    SCIConfig& config = m_sci_configs[unit];
    
    if (!config.interrupt_enabled) return;
    
    // Process transmit buffer
    if (!config.tx_buffer.empty()) {
        // Simulate transmission time
        // In a real implementation, this would be based on baud rate
    }
    
    // Process receive buffer
    if (!config.rx_buffer.empty()) {
        // Simulate reception time
        // In a real implementation, this would be based on baud rate
    }
}

void H8S2350PeripheralEmulator::updateADCChannel(uint8_t channel, uint32_t cycles) {
    ADCChannel& adc_channel = m_adc_channels[channel];
    
    if (!adc_channel.enabled) return;
    
    // Simulate conversion time
    if (!adc_channel.conversion_complete) {
        adc_channel.conversion_complete = true;
        adc_channel.interrupt_pending = adc_channel.interrupt_enabled;
        
        if (adc_channel.interrupt_pending) {
            triggerADCInterrupt(channel);
        }
    }
}

void H8S2350PeripheralEmulator::triggerTPUInterrupt(uint8_t unit, uint8_t channel) {
    // This would trigger an interrupt in the main CPU
    if (m_debug_enabled) {
        logMessage("TPU" + std::to_string(unit) + " Channel " + std::to_string(channel) + " interrupt");
    }
}

void H8S2350PeripheralEmulator::triggerSCIInterrupt(uint8_t unit, bool rx) {
    // This would trigger an interrupt in the main CPU
    if (m_debug_enabled) {
        logMessage("SCI" + std::to_string(unit) + " " + (rx ? "RX" : "TX") + " interrupt");
    }
}

void H8S2350PeripheralEmulator::triggerADCInterrupt(uint8_t channel) {
    // This would trigger an interrupt in the main CPU
    if (m_debug_enabled) {
        logMessage("ADC Channel " + std::to_string(channel) + " interrupt");
    }
}

void H8S2350PeripheralEmulator::logMessage(const std::string& message) {
    if (m_debug_enabled) {
        std::cout << "[Peripheral] " << message << std::endl;
    }
}

} // namespace MS2000
