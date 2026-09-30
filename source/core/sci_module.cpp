#include "sci_module.h"
#include <iostream>

namespace MS2000 {

SCIModule::SCIModule() {
    m_enabled.fill(false);
    m_rx_ready.fill(false);
    m_tx_ready.fill(true); // TX is ready when empty
}

void SCIModule::setEnabled(uint8_t channel, bool enabled) {
    if (channel < NUM_CHANNELS) {
        m_enabled[channel] = enabled;
        if (!enabled) {
            // Clear queues when disabled
            while (!m_rx_queues[channel].empty()) {
                m_rx_queues[channel].pop();
            }
            while (!m_tx_queues[channel].empty()) {
                m_tx_queues[channel].pop();
            }
            m_rx_ready[channel] = false;
            m_tx_ready[channel] = true;
        }
    }
}

bool SCIModule::isEnabled(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_enabled[channel];
    }
    return false;
}

bool SCIModule::hasData(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return !m_rx_queues[channel].empty();
    }
    return false;
}

uint8_t SCIModule::receiveData(uint8_t channel) {
    if (channel < NUM_CHANNELS && !m_rx_queues[channel].empty()) {
        uint8_t data = m_rx_queues[channel].front();
        m_rx_queues[channel].pop();
        m_rx_ready[channel] = !m_rx_queues[channel].empty();
        return data;
    }
    return 0;
}

void SCIModule::sendData(uint8_t channel, uint8_t data) {
    if (channel < NUM_CHANNELS && m_enabled[channel]) {
        m_tx_queues[channel].push(data);
        m_tx_ready[channel] = false;
    }
}

bool SCIModule::isRxReady(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_rx_ready[channel];
    }
    return false;
}

bool SCIModule::isTxReady(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_tx_ready[channel];
    }
    return false;
}

size_t SCIModule::getRxQueueSize(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_rx_queues[channel].size();
    }
    return 0;
}

size_t SCIModule::getTxQueueSize(uint8_t channel) const {
    if (channel < NUM_CHANNELS) {
        return m_tx_queues[channel].size();
    }
    return 0;
}

void SCIModule::update() {
    // For now, just print status
    // In a real implementation, this would handle actual serial communication
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        if (m_enabled[i]) {
            if (!m_rx_queues[i].empty()) {
                std::cout << "SCI " << i << ": RX data available (" << m_rx_queues[i].size() << " bytes)" << std::endl;
            }
            if (!m_tx_queues[i].empty()) {
                std::cout << "SCI " << i << ": TX data pending (" << m_tx_queues[i].size() << " bytes)" << std::endl;
            }
        }
    }
}

} // namespace MS2000
