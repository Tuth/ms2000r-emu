#include "ms2000_panel_adc.h"
#include <iostream>

namespace MS2000 {

MS2000PanelADC::MS2000PanelADC() {
    // Initialize with default state
    m_current_adsel = 0;
}

void MS2000PanelADC::setADSEL(uint8_t adsel_value) {
    m_current_adsel = adsel_value & 0x07; // Only 3 bits used
    
    if (m_debug_mode) {
        std::cout << "[Panel ADC] ADSEL = " << (int)m_current_adsel 
                  << " (channel " << (int)m_current_adsel << ")" << std::endl;
    }
}

uint16_t MS2000PanelADC::readAN4() {
    // BANK1: EG1/EG2 - channels 0..7 map to VR 0..7
    VR vr_id = getVRForBankChannel(1, m_current_adsel);
    return readADCValue(vr_id);
}

uint16_t MS2000PanelADC::readAN5() {
    // BANK2: LFO/EFF/PATCH - channels 0..7 map to VR 8..15  
    VR vr_id = getVRForBankChannel(2, m_current_adsel);
    return readADCValue(vr_id);
}

uint16_t MS2000PanelADC::readAN6() {
    // BANK3: OSC/MIX/PORTA - channels 0..7 map to VR 16..23
    VR vr_id = getVRForBankChannel(3, m_current_adsel);
    return readADCValue(vr_id);
}

uint16_t MS2000PanelADC::readAN7() {
    // BANK4: FILTER/AMP/ARP - channels 0..7 map to VR 24..31
    VR vr_id = getVRForBankChannel(4, m_current_adsel);
    return readADCValue(vr_id);
}

void MS2000PanelADC::processSettle() {
    // Called after ADSEL change to allow settling time
    // In real hardware, 20-50μs settle time is needed
    // In emulation, this is just for timing accuracy
    
    if (m_debug_mode) {
        std::cout << "[Panel ADC] Settle time processed for ADSEL=" << (int)m_current_adsel << std::endl;
    }
}

VR MS2000PanelADC::getVRForBankChannel(uint8_t bank, uint8_t channel) {
    // Map bank (1..4) and channel (0..7) to VR enum
    uint8_t vr_offset = ((bank - 1) * 8) + channel;
    
    if (vr_offset >= static_cast<uint8_t>(VR::VR_COUNT)) {
        return VR::EG1_ATTACK; // Default fallback
    }
    
    return static_cast<VR>(vr_offset);
}

uint16_t MS2000PanelADC::readADCValue(VR vr_id) {
    if (m_adc_read_callback) {
        uint16_t value = m_adc_read_callback(vr_id);
        
        if (m_debug_mode) {
            const char* vr_names[] = {
                "EG1_ATTACK", "EG1_DECAY", "EG1_SUSTAIN", "EG1_RELEASE",
                "EG2_ATTACK", "EG2_DECAY", "EG2_SUSTAIN", "EG2_RELEASE",
                "LFO1_FREQ", "LFO1_DEPTH", "LFO2_FREQ", "LFO2_DEPTH", 
                "EFF_SPEED", "EFF_DEPTH", "PATCH1", "PATCH2",
                "OSC1_CTRL1", "OSC1_SEMITONE", "OSC2_TUNE", "NOISE_LEVEL",
                "MIXER_OSC1", "MIXER_OSC2", "MIXER_NOISE", "PORTA_TIME", 
                "FILT_CUTOFF", "FILT_RESO", "FILT_KBD_TRK", "FILT_EG_INT",
                "AMP_LEVEL", "ARP_TEMPO", "ARP_GATE", "AMP_PAN"
            };
            
            int vr_idx = static_cast<int>(vr_id);
            if (vr_idx >= 0 && vr_idx < 32) {
                std::cout << "[Panel ADC] " << vr_names[vr_idx] << " = " << value 
                         << " (0x" << std::hex << value << std::dec << ")" << std::endl;
            }
        }
        
        return value & 0x0FFF; // 12-bit ADC
    }
    
    // Default value if no callback set
    return 0x800; // Mid-range (2048)
}

} // namespace MS2000