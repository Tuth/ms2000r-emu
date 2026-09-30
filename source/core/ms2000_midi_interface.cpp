#include "ms2000_midi_interface.h"
#include <iostream>
#include <iomanip>

namespace MS2000 {

MS2000MIDIInterface::MS2000MIDIInterface() {
    // Initialize with YAML spec reset values
    m_SMR = 0x00;   // Async mode, 8-bit, no parity, 1 stop bit
    m_BRR = 0xFF;   // Max baud rate register value
    m_SCR = 0x00;   // All interrupts disabled, TX/RX disabled
    m_SSR = 0x84;   // TDRE=1 (ready to transmit), TEND=1 (transmission complete)
    m_SCMR = 0xF2;  // LSB first, no inversion, normal SCI mode
}

void MS2000MIDIInterface::writeSMR(uint8_t value) {
    m_SMR = value;
    
    if (m_debug_mode) {
        std::cout << "[MIDI SCI] SMR = 0x" << std::hex << (int)value << std::dec;
        std::cout << " (async=" << ((value & 0x80) ? "sync" : "async");
        std::cout << ", bits=" << ((value & 0x40) ? "7" : "8");
        std::cout << ", parity=" << ((value & 0x20) ? "on" : "off") << ")" << std::endl;
    }
}

void MS2000MIDIInterface::writeBRR(uint8_t value) {
    m_BRR = value;
    
    if (m_debug_mode) {
        // Calculate approximate baud rate (simplified)
        // For MIDI: 31.25 kbps requires specific BRR value
        std::cout << "[MIDI SCI] BRR = " << (int)value;
        if (value == 39) { // Common value for 31.25 kbps at 20MHz
            std::cout << " (31.25 kbps - MIDI standard)";
        }
        std::cout << std::endl;
    }
}

void MS2000MIDIInterface::writeSCR(uint8_t value) {
    bool te_enable = (value & 0x20) != 0;
    bool re_enable = (value & 0x10) != 0;
    
    m_SCR = value;
    
    if (m_debug_mode) {
        std::cout << "[MIDI SCI] SCR = 0x" << std::hex << (int)value << std::dec;
        std::cout << " (TX=" << (te_enable ? "on" : "off");
        std::cout << ", RX=" << (re_enable ? "on" : "off");
        std::cout << ", TIE=" << (isTXInterruptEnabled() ? "on" : "off");
        std::cout << ", RIE=" << (isRXInterruptEnabled() ? "on" : "off") << ")" << std::endl;
    }
    
    updateSSRFlags();
}

void MS2000MIDIInterface::writeTDR(uint8_t value) {
    if (isTransmitterEnabled()) {
        // Add byte to transmit queue
        m_tx_queue.push(value);
        
        // Clear TDRE flag (transmitter busy)
        m_SSR &= ~0x80;
        
        if (m_debug_mode) {
            std::cout << "[MIDI SCI] TX: 0x" << std::hex << (int)value 
                     << std::dec << " (" << (int)value << ")" << std::endl;
        }
    }
}

void MS2000MIDIInterface::writeSCMR(uint8_t value) {
    m_SCMR = value;
    
    if (m_debug_mode) {
        std::cout << "[MIDI SCI] SCMR = 0x" << std::hex << (int)value << std::dec;
        std::cout << " (order=" << ((value & 0x08) ? "MSB" : "LSB");
        std::cout << ", invert=" << ((value & 0x04) ? "on" : "off") << ")" << std::endl;
    }
}

uint8_t MS2000MIDIInterface::readSMR() const {
    return m_SMR;
}

uint8_t MS2000MIDIInterface::readBRR() const {
    return m_BRR;
}

uint8_t MS2000MIDIInterface::readSCR() const {
    return m_SCR;
}

uint8_t MS2000MIDIInterface::readSSR() const {
    return m_SSR;
}

uint8_t MS2000MIDIInterface::readRDR() {
    uint8_t value = 0x00;
    
    if (!m_rx_queue.empty()) {
        value = m_rx_queue.front();
        m_rx_queue.pop();
        
        // Clear RDRF flag if queue is now empty
        if (m_rx_queue.empty()) {
            m_SSR &= ~0x40;
        }
        
        if (m_debug_mode) {
            std::cout << "[MIDI SCI] RX: 0x" << std::hex << (int)value 
                     << std::dec << " (" << (int)value << ")" << std::endl;
        }
        
        m_rx_byte_count++;
    }
    
    return value;
}

uint8_t MS2000MIDIInterface::readSCMR() const {
    return m_SCMR;
}

void MS2000MIDIInterface::receiveMIDIData(const uint8_t* data, size_t length) {
    if (!isReceiverEnabled()) {
        return; // Receiver is disabled
    }
    
    for (size_t i = 0; i < length; i++) {
        // Add to receive queue
        m_rx_queue.push(data[i]);
        
        // Set RDRF flag (receive data ready)
        m_SSR |= 0x40;
        
        // Parse MIDI message
        parseMIDIMessage(data[i]);
    }
    
    if (m_debug_mode && length > 0) {
        std::cout << "[MIDI SCI] Received " << length << " bytes from host MIDI" << std::endl;
    }
}

void MS2000MIDIInterface::processTiming(uint32_t current_time_ms) {
    processTransmit(current_time_ms);
    processReceive(current_time_ms);
}

void MS2000MIDIInterface::processTransmit(uint32_t current_time_ms) {
    // Process transmit queue at MIDI speed (31.25 kbps = ~1ms per byte)
    if (!m_tx_queue.empty() && isTransmitterEnabled()) {
        if (current_time_ms - m_last_tx_time >= MIDI_BYTE_TIME_MS) {
            uint8_t byte = m_tx_queue.front();
            m_tx_queue.pop();
            
            // Send to host MIDI
            if (m_midi_output_callback) {
                m_midi_output_callback(&byte, 1);
            }
            
            m_tx_byte_count++;
            m_last_tx_time = current_time_ms;
            
            // Set TDRE flag if queue is empty (ready for next byte)
            if (m_tx_queue.empty()) {
                m_SSR |= 0x80;  // TDRE = 1
                m_SSR |= 0x04;  // TEND = 1
            }
        }
    }
}

void MS2000MIDIInterface::processReceive(uint32_t current_time_ms) {
    // Update receive timing (for accurate simulation)
    m_last_rx_time = current_time_ms;
}

void MS2000MIDIInterface::updateSSRFlags() {
    // Update status flags based on current state
    
    // TDRE flag - set if transmitter is enabled and queue has space
    if (isTransmitterEnabled() && m_tx_queue.size() < 8) {
        m_SSR |= 0x80;  // TDRE = 1
    }
    
    // RDRF flag - set if receiver has data
    if (!m_rx_queue.empty()) {
        m_SSR |= 0x40;  // RDRF = 1
    }
    
    // TEND flag - set if transmit queue is empty
    if (m_tx_queue.empty()) {
        m_SSR |= 0x04;  // TEND = 1
    } else {
        m_SSR &= ~0x04; // TEND = 0
    }
}

void MS2000MIDIInterface::parseMIDIMessage(uint8_t byte) {
    if (isSystemRealTime(byte)) {
        // System real-time messages are single bytes, process immediately
        MIDIMessage msg(&byte, 1);
        msg.timestamp_ms = m_last_rx_time;
        
        if (m_midi_input_callback) {
            m_midi_input_callback(msg);
        }
        return;
    }
    
    if (isStatusByte(byte)) {
        // Process previous message if complete
        if (!m_current_message.empty()) {
            processMIDIMessage();
        }
        
        // Start new message
        m_current_message.clear();
        m_current_message.push_back(byte);
        
        if (!isSystemCommon(byte)) {
            m_running_status = byte; // Save running status
        }
    } else {
        // Data byte
        if (m_current_message.empty() && m_running_status != 0) {
            // Use running status
            m_current_message.push_back(m_running_status);
        }
        
        if (!m_current_message.empty()) {
            m_current_message.push_back(byte);
            
            // Check if message is complete
            size_t expected = getExpectedDataBytes(m_current_message[0]);
            if (m_current_message.size() - 1 >= expected) {
                processMIDIMessage();
            }
        }
    }
}

void MS2000MIDIInterface::processMIDIMessage() {
    if (!m_current_message.empty() && m_midi_input_callback) {
        MIDIMessage msg;
        msg.data = m_current_message;
        msg.timestamp_ms = m_last_rx_time;
        
        m_midi_input_callback(msg);
        
        if (m_debug_mode) {
            std::cout << "[MIDI] Complete message: ";
            for (uint8_t b : m_current_message) {
                std::cout << "0x" << std::hex << (int)b << " ";
            }
            std::cout << std::dec << std::endl;
        }
    }
    
    m_current_message.clear();
}

size_t MS2000MIDIInterface::getExpectedDataBytes(uint8_t status) const {
    uint8_t command = status & 0xF0;
    
    switch (command) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: // 2 data bytes
            return 2;
        case 0xC0: case 0xD0: // 1 data byte
            return 1;
        case 0xF0: // System exclusive - variable length
            return 0; // Will be terminated by 0xF7
        default:
            return 0;
    }
}

} // namespace MS2000