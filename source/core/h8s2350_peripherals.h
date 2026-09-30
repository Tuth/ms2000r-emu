#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <queue>

namespace MS2000 {

// System Control Register (based on MAME H8S/2357)
struct SystemControlRegister {
    uint8_t SYSCR;  // System Control Register
};

// TPU (Timer Pulse Unit) Registers (updated based on MAME)
struct TPURegisters {
    uint16_t TCR;    // Timer Control Register
    uint16_t TSR;    // Timer Status Register
    uint16_t TCNT;   // Timer Counter
    uint16_t GR;     // General Register
    uint16_t ICR;    // Input Capture Register
    uint16_t OCR;    // Output Compare Register
    uint16_t TSTR;   // Timer Start Register
    uint16_t TSYR;   // Timer Synchronization Register
    uint16_t TMDR;   // Timer Mode Register (added from MAME)
    uint16_t TIOR;   // Timer I/O Control Register (added from MAME)
    uint16_t TIER;   // Timer Interrupt Enable Register (added from MAME)
};

// TPU Channel Configuration
struct TPUChannel {
    bool enabled;
    uint8_t mode;           // Input Capture, Output Compare, PWM, etc.
    uint16_t compare_value;
    uint16_t capture_value;
    uint32_t frequency;     // For PWM mode
    uint8_t duty_cycle;     // For PWM mode (0-100)
    bool interrupt_enabled;
    bool interrupt_pending;
};

// SCI (Serial Communication Interface) Registers (updated based on MAME)
struct SCIRegisters {
    uint8_t SMR;    // Serial Mode Register
    uint8_t BRR;    // Bit Rate Register
    uint8_t SCR;    // Serial Control Register
    uint8_t TDR;    // Transmit Data Register
    uint8_t SSR;    // Serial Status Register
    uint8_t RDR;    // Receive Data Register
    uint8_t SCMR;   // Serial Control Mode Register (added from MAME)
};

// SCI Configuration (updated based on MAME)
struct SCIConfig {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    bool parity_enabled;
    bool parity_even;
    bool interrupt_enabled;
    std::queue<uint8_t> rx_buffer;
    std::queue<uint8_t> tx_buffer;
    
    // MAME SCI flags
    enum {
        SMR_CA   = 0x80,  // Communication Mode
        SMR_CHR  = 0x40,  // Character Length
        SMR_PE   = 0x20,  // Parity Enable
        SMR_OE   = 0x10,  // Odd/Even Parity
        SMR_STOP = 0x08,  // Stop Bit Length
        SMR_MP   = 0x04,  // Multiprocessor Mode
        SMR_CKS  = 0x03,  // Clock Select
        
        SCR_TIE  = 0x80,  // Transmit Interrupt Enable
        SCR_RIE  = 0x40,  // Receive Interrupt Enable
        SCR_TE   = 0x20,  // Transmit Enable
        SCR_RE   = 0x10,  // Receive Enable
        SCR_MPIE = 0x08,  // Multiprocessor Interrupt Enable
        SCR_TEIE = 0x04,  // Transmit End Interrupt Enable
        SCR_CKE  = 0x03,  // Clock Enable
        
        SSR_TDRE = 0x80,  // Transmit Data Register Empty
        SSR_RDRF = 0x40,  // Receive Data Register Full
        SSR_ORER = 0x20,  // Overrun Error
        SSR_FER  = 0x10,  // Framing Error
        SSR_PER  = 0x08,  // Parity Error
        SSR_TEND = 0x04,  // Transmit End
        SSR_MPB  = 0x02,  // Multiprocessor Bit
        SSR_MPBT = 0x01   // Multiprocessor Bit Transfer
    };
};

// ADC (Analog-to-Digital Converter) Registers (updated based on MAME)
struct ADCRegisters {
    uint8_t ADCSR;  // ADC Control/Status Register
    uint8_t ADCR;   // ADC Control Register
    uint8_t ADDR;   // ADC Data Register
    uint8_t ADDRH;  // ADC Data Register High
    uint8_t ADDR8;  // ADC Data Register (8-bit) - added from MAME
};

// ADC Channel Configuration
struct ADCChannel {
    bool enabled;
    uint16_t value;         // Current ADC value (0-1023)
    bool conversion_complete;
    bool interrupt_enabled;
    bool interrupt_pending;
};

// DAC (Digital-to-Analog Converter) Registers
struct DACRegisters {
    uint16_t DADR;  // DAC Data Register
    uint8_t DACR;   // DAC Control Register
};

// GPIO Port Configuration (updated based on MAME)
struct GPIOPort {
    uint8_t DDR;    // Data Direction Register
    uint8_t DR;     // Data Register
    uint8_t PDR;    // Port Data Register
    uint8_t PCR;    // Port Control Register
    uint8_t ODR;    // Open Drain Register (added from MAME)
    uint8_t FF;     // Port Function Register (added from MAME)
};

// LED Matrix Configuration
struct LEDMatrix {
    std::array<std::array<bool, 8>, 8> matrix;
    uint8_t current_row;
    uint8_t current_column_mask;
};

// Button Matrix Configuration
struct ButtonMatrix {
    std::array<std::array<bool, 8>, 8> matrix;
    uint8_t current_row;
    uint8_t scan_mask;
};

// DMA (Direct Memory Access) Registers (added from MAME)
struct DMARegisters {
    uint16_t DMACR;     // DMA Control Register
    uint32_t MARAH;     // Memory Address Register A High
    uint32_t MARAL;     // Memory Address Register A Low
    uint16_t IOARA;     // I/O Address Register A
    uint16_t ETCRA;     // Extended Transfer Control Register A
    uint32_t MARBH;     // Memory Address Register B High
    uint32_t MARBL;     // Memory Address Register B Low
    uint16_t IOARB;     // I/O Address Register B
    uint16_t ETCRB;     // Extended Transfer Control Register B
};

// DMA Configuration
struct DMAConfig {
    bool enabled;
    uint32_t source_address;
    uint32_t destination_address;
    uint16_t transfer_count;
    bool interrupt_enabled;
    bool interrupt_pending;
};

// Interrupt Controller Registers (added from MAME)
struct InterruptRegisters {
    uint8_t ISCRH;      // Interrupt Sense Control Register High
    uint8_t ISCRL;      // Interrupt Sense Control Register Low
    uint8_t IER;        // Interrupt Enable Register
    uint8_t ISR;        // Interrupt Status Register
    uint8_t IPR[13];    // Interrupt Priority Registers
};

// Watchdog Registers (added from MAME)
struct WatchdogRegisters {
    uint16_t WD;        // Watchdog Register
    uint16_t RST;       // Reset Register
};

// Timer8 Registers (added from MAME)
struct Timer8Registers {
    uint8_t TCR;        // Timer Control Register
    uint8_t TCSR;       // Timer Control/Status Register
    uint8_t TCNT;       // Timer Counter
    uint16_t TCOR;      // Timer Constant Register
};

// Timer16 Registers (updated based on MAME)
struct Timer16Registers {
    uint8_t TCR;        // Timer Control Register
    uint8_t TMDR;       // Timer Mode Register
    uint16_t TIOR;      // Timer I/O Control Register
    uint8_t TIER;       // Timer Interrupt Enable Register
    uint8_t TSR;        // Timer Status Register
    uint16_t TCNT;      // Timer Counter
    uint16_t GR[4];     // General Registers
};

// Peripheral Emulator Class
class H8S2350PeripheralEmulator {
private:
    // TPU Units (6 channels each)
    std::array<TPURegisters, 6> m_tpu_registers;
    std::array<TPUChannel, 36> m_tpu_channels;  // 6 units * 6 channels
    
    // SCI Units (2 channels)
    std::array<SCIRegisters, 2> m_sci_registers;
    std::array<SCIConfig, 2> m_sci_configs;
    
    // ADC Unit
    ADCRegisters m_adc_registers;
    std::array<ADCChannel, 8> m_adc_channels;  // AN0-AN7
    
    // DAC Unit
    DACRegisters m_dac_registers;
    uint16_t m_dac_output;
    
    // GPIO Ports
    std::array<GPIOPort, 8> m_gpio_ports;  // Ports A-H
    
    // LED and Button Matrices
    LEDMatrix m_led_matrix;
    ButtonMatrix m_button_matrix;
    
    // Callbacks
    std::function<void(uint8_t)> m_midi_rx_callback;
    std::function<void(uint8_t)> m_midi_tx_callback;
    std::function<void(uint16_t)> m_audio_output_callback;
    std::function<void(int, int, bool)> m_button_callback;
    std::function<void(int, uint16_t)> m_potentiometer_callback;
    
    // Timing
    uint32_t m_clock_frequency;
    uint32_t m_cycle_count;
    
    // Debug
    bool m_debug_enabled;

public:
    H8S2350PeripheralEmulator();
    ~H8S2350PeripheralEmulator();
    
    // Initialization
    bool initialize(uint32_t clock_frequency = 20000000);
    void reset();
    void setDebug(bool enabled) { m_debug_enabled = enabled; }
    
    // TPU Functions
    uint16_t readTPURegister(uint8_t unit, uint8_t reg);
    void writeTPURegister(uint8_t unit, uint8_t reg, uint16_t value);
    void updateTPU(uint32_t cycles);
    void setTPUInterruptCallback(std::function<void(uint8_t)> callback);
    
    // SCI Functions
    uint8_t readSCIRegister(uint8_t unit, uint8_t reg);
    void writeSCIRegister(uint8_t unit, uint8_t reg, uint8_t value);
    void updateSCI(uint32_t cycles);
    void setMIDICallbacks(std::function<void(uint8_t)> rx_callback, 
                         std::function<void(uint8_t)> tx_callback);
    void sendMIDIByte(uint8_t unit, uint8_t data);
    uint8_t receiveMIDIByte(uint8_t unit);
    
    // ADC Functions
    uint8_t readADCRegister(uint8_t reg);
    void writeADCRegister(uint8_t reg, uint8_t value);
    void setADCValue(uint8_t channel, uint16_t value);
    uint16_t getADCValue(uint8_t channel);
    void startADCConversion(uint8_t channel);
    void updateADC(uint32_t cycles);
    
    // DAC Functions
    uint16_t readDACRegister(uint8_t reg);
    void writeDACRegister(uint8_t reg, uint16_t value);
    uint16_t getDACOutput() const { return m_dac_output; }
    void setAudioOutputCallback(std::function<void(uint16_t)> callback);
    
    // GPIO Functions
    uint8_t readGPIOPort(uint8_t port, uint8_t reg);
    void writeGPIOPort(uint8_t port, uint8_t reg, uint8_t value);
    void setGPIOInput(uint8_t port, uint8_t pin, bool value);
    bool getGPIOOutput(uint8_t port, uint8_t pin);
    
    // LED Matrix Functions
    void setLED(uint8_t row, uint8_t col, bool state);
    bool getLED(uint8_t row, uint8_t col);
    void updateLEDMatrix();
    
    // Button Matrix Functions
    void setButton(uint8_t row, uint8_t col, bool pressed);
    bool getButton(uint8_t row, uint8_t col);
    void updateButtonMatrix();
    void setButtonCallback(std::function<void(int, int, bool)> callback);
    
    // Potentiometer Functions
    void setPotentiometerValue(uint8_t channel, uint16_t value);
    uint16_t getPotentiometerValue(uint8_t channel);
    void setPotentiometerCallback(std::function<void(int, uint16_t)> callback);
    
    // Main Update Function
    void update(uint32_t cycles);
    
    // Debug Functions
    void dumpTPUStatus();
    void dumpSCIStatus();
    void dumpADCStatus();
    void dumpDACStatus();
    void dumpGPIOStatus();
    void dumpLEDMatrix();
    void dumpButtonMatrix();
    
private:
    // Helper Functions
    void initializeTPU();
    void initializeSCI();
    void initializeADC();
    void initializeDAC();
    void initializeGPIO();
    void initializeMatrices();
    
    void updateTPUChannel(uint8_t unit, uint8_t channel, uint32_t cycles);
    void updateSCIChannel(uint8_t unit, uint32_t cycles);
    void updateADCChannel(uint8_t channel, uint32_t cycles);
    
    void triggerTPUInterrupt(uint8_t unit, uint8_t channel);
    void triggerSCIInterrupt(uint8_t unit, bool rx);
    void triggerADCInterrupt(uint8_t channel);
    
    void logMessage(const std::string& message);
};

} // namespace MS2000
