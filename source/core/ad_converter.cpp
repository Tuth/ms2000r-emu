#include "ad_converter.h"
#include <iostream>

namespace MS2000 {

ADConverter::ADConverter() : m_current_channel(0), m_conversion_complete(false), m_enabled(true) {
    m_channel_values.fill(0);
}

void ADConverter::setChannel(uint8_t channel) {
    if (channel < NUM_CHANNELS) {
        m_current_channel = channel;
    }
}

void ADConverter::startConversion() {
    if (m_enabled) {
        m_conversion_complete = false;
        // In a real implementation, this would start the actual conversion
        // For now, we'll simulate it by setting complete immediately
        m_conversion_complete = true;
    }
}

uint16_t ADConverter::readChannel(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_channel_values[channel];
    }
    return 0;
}

void ADConverter::writeChannel(uint8_t channel, uint16_t value) {
    if (channel < NUM_CHANNELS) {
        m_channel_values[channel] = value & 0x3FF; // 10-bit ADC
    }
}

uint16_t ADConverter::readCurrentChannel() const {
    return readChannel(m_current_channel);
}

void ADConverter::update() {
    if (!m_enabled) {
        return;
    }
    
    // For now, just print status
    // In a real implementation, this would handle actual ADC conversion
    if (m_conversion_complete) {
        std::cout << "ADC: Channel " << (int)m_current_channel 
                  << " = " << m_channel_values[m_current_channel] << std::endl;
    }
}

} // namespace MS2000
