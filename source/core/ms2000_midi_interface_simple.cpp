#include "ms2000_midi_interface_simple.h"
#include <iostream>

namespace MS2000 {

MS2000MIDIInterfaceSimple::MS2000MIDIInterfaceSimple() {
    // Simple initialization
}

void MS2000MIDIInterfaceSimple::writeTDR(uint8_t value) {
    // Add byte to transmit queue
    m_tx_queue.push(value);
    
    if (m_debug_mode) {
        std::cout << "[MIDI] TX: 0x" << std::hex << (int)value << std::dec << std::endl;
    }
}

uint8_t MS2000MIDIInterfaceSimple::readRDR() {
    uint8_t value = 0x00;
    
    if (!m_rx_queue.empty()) {
        value = m_rx_queue.front();
        m_rx_queue.pop();
        
        if (m_debug_mode) {
            std::cout << "[MIDI] RX: 0x" << std::hex << (int)value << std::dec << std::endl;
        }
    }
    
    return value;
}

uint8_t MS2000MIDIInterfaceSimple::readSSR() const {
    uint8_t ssr = 0x84; // TDRE=1 (ready to transmit), TEND=1 (transmission complete)
    
    if (!m_rx_queue.empty()) {
        ssr |= 0x40; // RDRF = 1 (receive data ready)
    }
    
    return ssr;
}

void MS2000MIDIInterfaceSimple::receiveMIDIData(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        m_rx_queue.push(data[i]);
    }
    
    if (m_debug_mode && length > 0) {
        std::cout << "[MIDI] Received " << length << " bytes" << std::endl;
    }
    
    // Forward to callback
    if (m_midi_input_callback) {
        m_midi_input_callback(data, length);
    }
}

void MS2000MIDIInterfaceSimple::processTiming(uint32_t current_time_ms) {
    // Process transmit queue
    if (!m_tx_queue.empty()) {
        if (current_time_ms - m_last_tx_time >= 1) { // ~1ms per byte
            uint8_t byte = m_tx_queue.front();
            m_tx_queue.pop();
            
            // Send to host MIDI
            if (m_midi_output_callback) {
                m_midi_output_callback(&byte, 1);
            }
            
            m_last_tx_time = current_time_ms;
        }
    }
}

} // namespace MS2000