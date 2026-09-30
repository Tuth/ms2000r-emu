#pragma once

#include <cstdint>
#include <array>

namespace MS2000 {

class IOPorts {
private:
    static constexpr size_t NUM_PORTS = 8;
    std::array<uint8_t, NUM_PORTS> m_port_data;
    std::array<uint8_t, NUM_PORTS> m_port_direction; // 0=input, 1=output
    std::array<uint8_t, NUM_PORTS> m_port_pullup;    // 0=disabled, 1=enabled
    
public:
    IOPorts();
    ~IOPorts() = default;
    
    // Port control
    void setPortDirection(uint8_t port, uint8_t direction);
    uint8_t getPortDirection(uint8_t port) const;
    void setPullup(uint8_t port, bool enabled);
    bool getPullup(uint8_t port) const;
    
    // Port data access
    uint8_t readPort(uint8_t port) const;
    void writePort(uint8_t port, uint8_t value);
    void setPortBit(uint8_t port, uint8_t bit, bool value);
    bool getPortBit(uint8_t port, uint8_t bit) const;
    
    // Port properties
    size_t getNumPorts() const { return NUM_PORTS; }
    
    // Update ports (called by MCU)
    void update();
};

} // namespace MS2000
