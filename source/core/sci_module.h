#pragma once

#include <cstdint>
#include <queue>
#include <array>

namespace MS2000 {

class SCIModule {
private:
    static constexpr size_t NUM_CHANNELS = 2;
    std::array<std::queue<uint8_t>, NUM_CHANNELS> m_rx_queues;
    std::array<std::queue<uint8_t>, NUM_CHANNELS> m_tx_queues;
    std::array<bool, NUM_CHANNELS> m_enabled;
    std::array<bool, NUM_CHANNELS> m_rx_ready;
    std::array<bool, NUM_CHANNELS> m_tx_ready;
    
public:
    SCIModule();
    ~SCIModule() = default;
    
    // SCI control
    void setEnabled(uint8_t channel, bool enabled);
    bool isEnabled(uint8_t channel) const;
    
    // Receive operations
    bool hasData(uint8_t channel) const;
    uint8_t receiveData(uint8_t channel);
    void sendData(uint8_t channel, uint8_t data);
    bool isRxReady(uint8_t channel) const;
    bool isTxReady(uint8_t channel) const;
    
    // Status
    size_t getRxQueueSize(uint8_t channel) const;
    size_t getTxQueueSize(uint8_t channel) const;
    
    // SCI properties
    size_t getNumChannels() const { return NUM_CHANNELS; }
    
    // Update SCI (called by MCU)
    void update();
};

} // namespace MS2000
