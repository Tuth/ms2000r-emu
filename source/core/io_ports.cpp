#include "io_ports.h"
#include <iostream>

namespace MS2000 {

IOPorts::IOPorts() {
    // Initialize all ports as inputs with pullup disabled
    m_port_data.fill(0xFF);
    m_port_direction.fill(0x00);
    m_port_pullup.fill(0x00);
}

void IOPorts::setPortDirection(uint8_t port, uint8_t direction) {
    if (port < NUM_PORTS) {
        m_port_direction[port] = direction;
    }
}

uint8_t IOPorts::getPortDirection(uint8_t port) const {
    if (port < NUM_PORTS) {
        return m_port_direction[port];
    }
    return 0;
}

void IOPorts::setPullup(uint8_t port, bool enabled) {
    if (port < NUM_PORTS) {
        m_port_pullup[port] = enabled ? 0xFF : 0x00;
    }
}

bool IOPorts::getPullup(uint8_t port) const {
    if (port < NUM_PORTS) {
        return m_port_pullup[port] != 0;
    }
    return false;
}

uint8_t IOPorts::readPort(uint8_t port) const {
    if (port < NUM_PORTS) {
        return m_port_data[port];
    }
    return 0xFF;
}

void IOPorts::writePort(uint8_t port, uint8_t value) {
    if (port < NUM_PORTS) {
        m_port_data[port] = value;
    }
}

void IOPorts::setPortBit(uint8_t port, uint8_t bit, bool value) {
    if (port < NUM_PORTS && bit < 8) {
        if (value) {
            m_port_data[port] |= (1 << bit);
        } else {
            m_port_data[port] &= ~(1 << bit);
        }
    }
}

bool IOPorts::getPortBit(uint8_t port, uint8_t bit) const {
    if (port < NUM_PORTS && bit < 8) {
        return (m_port_data[port] & (1 << bit)) != 0;
    }
    return false;
}

void IOPorts::update() {
    // For now, just print status
    // In a real implementation, this would handle actual I/O
    for (size_t i = 0; i < NUM_PORTS; ++i) {
        if (m_port_direction[i] != 0) {
            std::cout << "IOPort " << i << ": " << std::hex << (int)m_port_data[i] << std::dec << std::endl;
        }
    }
}

} // namespace MS2000
