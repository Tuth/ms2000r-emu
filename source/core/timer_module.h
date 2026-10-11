#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace MS2000 {

class TimerModule {
private:
    static constexpr size_t NUM_CHANNELS = 2;
    std::array<uint16_t, NUM_CHANNELS> m_counter_values;
    std::array<uint16_t, NUM_CHANNELS> m_compare_values;
    std::array<bool, NUM_CHANNELS> m_enabled;
    std::array<bool, NUM_CHANNELS> m_compare_match;
    
public:
    TimerModule();
    ~TimerModule() = default;
    
    // Timer control
    void setEnabled(uint8_t channel, bool enabled);
    bool isEnabled(uint8_t channel) const;
    
    // Counter access
    uint16_t readCounter(uint8_t channel) const;
    void writeCounter(uint8_t channel, uint16_t value);
    void incrementCounter(uint8_t channel);
    
    // Compare value access
    uint16_t readCompare(uint8_t channel) const;
    void writeCompare(uint8_t channel, uint16_t value);
    bool isCompareMatch(uint8_t channel) const;
    void clearCompareMatch(uint8_t channel);
    
    // Timer properties
    size_t getNumChannels() const { return NUM_CHANNELS; }
    
    // Update timer (called by MCU)
    void update();
};

} // namespace MS2000
