#pragma once

#include <cstdint>
#include <array>

namespace MS2000 {

class ADConverter {
private:
    static constexpr size_t NUM_CHANNELS = 8;
    std::array<uint16_t, NUM_CHANNELS> m_channel_values;
    uint8_t m_current_channel;
    bool m_conversion_complete;
    bool m_enabled;
    
public:
    ADConverter();
    ~ADConverter() = default;
    
    // ADC control
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setChannel(uint8_t channel);
    uint8_t getChannel() const { return m_current_channel; }
    
    // Conversion control
    void startConversion();
    bool isConversionComplete() const { return m_conversion_complete; }
    void setConversionComplete(bool complete) { m_conversion_complete = complete; }
    
    // Channel data access
    uint16_t readChannel(uint8_t channel) const;
    void writeChannel(uint8_t channel, uint16_t value);
    uint16_t readCurrentChannel() const;
    
    // ADC properties
    size_t getNumChannels() const { return NUM_CHANNELS; }
    
    // Update ADC (called by MCU)
    void update();
};

} // namespace MS2000
