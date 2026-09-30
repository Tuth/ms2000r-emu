#include "timer_module.h"
#include <iostream>

namespace MS2000 {

TimerModule::TimerModule() {
    m_counter_values.fill(0);
    m_compare_values.fill(0xFFFF);
    m_enabled.fill(false);
    m_compare_match.fill(false);
}

void TimerModule::setEnabled(uint8_t channel, bool enabled) {
    if (channel < NUM_CHANNELS) {
        m_enabled[channel] = enabled;
        if (!enabled) {
            m_counter_values[channel] = 0;
            m_compare_match[channel] = false;
        }
    }
}

bool TimerModule::isEnabled(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_enabled[channel];
    }
    return false;
}

uint16_t TimerModule::readCounter(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_counter_values[channel];
    }
    return 0;
}

void TimerModule::writeCounter(uint8_t channel, uint16_t value) {
    if (channel < NUM_CHANNELS) {
        m_counter_values[channel] = value;
    }
}

void TimerModule::incrementCounter(uint8_t channel) {
    if (channel < NUM_CHANNELS && m_enabled[channel]) {
        m_counter_values[channel]++;
        
        // Check for compare match
        if (m_counter_values[channel] == m_compare_values[channel]) {
            m_compare_match[channel] = true;
        }
    }
}

uint16_t TimerModule::readCompare(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_compare_values[channel];
    }
    return 0xFFFF;
}

void TimerModule::writeCompare(uint8_t channel, uint16_t value) {
    if (channel < NUM_CHANNELS) {
        m_compare_values[channel] = value;
    }
}

bool TimerModule::isCompareMatch(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_compare_match[channel];
    }
    return false;
}

void TimerModule::clearCompareMatch(uint8_t channel) {
    if (channel < NUM_CHANNELS) {
        m_compare_match[channel] = false;
    }
}

void TimerModule::update() {
    // For now, just print status
    // In a real implementation, this would handle actual timer ticks
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        if (m_enabled[i] && m_compare_match[i]) {
            std::cout << "Timer " << i << ": Compare match at " << m_counter_values[i] << std::endl;
        }
    }
}

} // namespace MS2000
