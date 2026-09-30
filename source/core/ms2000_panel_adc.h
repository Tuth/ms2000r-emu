#pragma once

#include <cstdint>
#include <functional>

namespace MS2000 {

// MS2000 Panel ADC System - 32 potentiometers via 4x HC4051 multiplexers
// Based on hardware specification: 3x ADSEL[2:0] -> 4x BANK (AN4..AN7)

enum class VR : uint8_t {
    // BANK1 (AN4): EG1/EG2
    EG1_ATTACK = 0,  EG1_DECAY = 1,  EG1_SUSTAIN = 2,  EG1_RELEASE = 3,
    EG2_ATTACK = 4,  EG2_DECAY = 5,  EG2_SUSTAIN = 6,  EG2_RELEASE = 7,
    
    // BANK2 (AN5): LFO/EFF/PATCH  
    LFO1_FREQ = 8,   LFO1_DEPTH = 9,  LFO2_FREQ = 10,   LFO2_DEPTH = 11,
    EFF_SPEED = 12,  EFF_DEPTH = 13,  PATCH1 = 14,      PATCH2 = 15,
    
    // BANK3 (AN6): OSC/MIX/PORTA
    OSC1_CTRL1 = 16,    OSC1_SEMITONE = 17, OSC2_TUNE = 18,     NOISE_LEVEL = 19,
    MIXER_OSC1 = 20,    MIXER_OSC2 = 21,    MIXER_NOISE = 22,   PORTA_TIME = 23,
    
    // BANK4 (AN7): FILTER/AMP/ARP
    FILT_CUTOFF = 24,   FILT_RESO = 25,     FILT_KBD_TRK = 26,  FILT_EG_INT = 27,
    AMP_LEVEL = 28,     ARP_TEMPO = 29,     ARP_GATE = 30,      AMP_PAN = 31,
    
    VR_COUNT = 32
};

// ADC read callback: (VR id) -> 0..4095 ADC value
using ADCReadCallback = std::function<uint16_t(VR vr_id)>;

class MS2000PanelADC {
public:
    MS2000PanelADC();
    
    // Set GUI callback for reading potentiometer values
    void setADCReadCallback(ADCReadCallback callback) { m_adc_read_callback = callback; }
    
    // MCU writes to ADSEL[2:0] to select channel 0..7
    void setADSEL(uint8_t adsel_value);
    
    // MCU reads from AN4..AN7 to get current selected channel values
    uint16_t readAN4(); // BANK1
    uint16_t readAN5(); // BANK2  
    uint16_t readAN6(); // BANK3
    uint16_t readAN7(); // BANK4
    
    // Get current ADSEL value
    uint8_t getADSEL() const { return m_current_adsel; }
    
    // Process timing - call with settle time after ADSEL change
    void processSettle();
    
    // Debug info
    void setDebugMode(bool enabled) { m_debug_mode = enabled; }

private:
    uint8_t m_current_adsel = 0;      // Current ADSEL[2:0] value (0..7)
    ADCReadCallback m_adc_read_callback = nullptr;
    bool m_debug_mode = false;
    
    // Get VR ID for given bank and channel
    VR getVRForBankChannel(uint8_t bank, uint8_t channel);
    
    // Read ADC value with callback or return default
    uint16_t readADCValue(VR vr_id);
};

} // namespace MS2000