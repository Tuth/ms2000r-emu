#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <queue>

namespace MS2000 {

// Simple MIDI Interface for MS2000 - avoids conflicts with existing MIDI system
// This is a basic SCI0-based MIDI interface for the GUI API

class MS2000MIDIInterfaceSimple {
public:
    MS2000MIDIInterfaceSimple();
    
    // Host MIDI callbacks
    void setMIDIOutputCallback(std::function<void(const uint8_t*, size_t)> callback) { 
        m_midi_output_callback = callback; 
    }
    void setMIDIInputCallback(std::function<void(const uint8_t*, size_t)> callback) { 
        m_midi_input_callback = callback; 
    }
    
    // SCI0 Register Interface (called by H8S emulator)
    void writeTDR(uint8_t value);   // Transmit Data Register
    uint8_t readRDR();              // Receive Data Register
    uint8_t readSSR() const;        // Serial Status Register
    
    // Additional register methods for compatibility
    uint8_t readSMR() const { return 0x00; }   // Serial Mode Register
    uint8_t readBRR() const { return 0xFF; }   // Bit Rate Register
    uint8_t readSCR() const { return 0x00; }   // Serial Control Register
    uint8_t readSCMR() const { return 0xF2; }  // Serial Control Mode Register
    void writeSMR(uint8_t /*value*/) { /* stub */ }
    void writeBRR(uint8_t /*value*/) { /* stub */ }
    void writeSCR(uint8_t /*value*/) { /* stub */ }
    void writeSCMR(uint8_t /*value*/) { /* stub */ }
    
    // Host MIDI input (from external MIDI devices)
    void receiveMIDIData(const uint8_t* data, size_t length);
    
    // Process timing - call periodically for UART simulation
    void processTiming(uint32_t current_time_ms);
    
    // Debug info
    void setDebugMode(bool enabled) { m_debug_mode = enabled; }

private:
    // UART simulation
    std::queue<uint8_t> m_tx_queue;     // Transmit queue
    std::queue<uint8_t> m_rx_queue;     // Receive queue
    
    uint32_t m_last_tx_time = 0;        // Last transmit time
    
    // Callbacks
    std::function<void(const uint8_t*, size_t)> m_midi_output_callback = nullptr;
    std::function<void(const uint8_t*, size_t)> m_midi_input_callback = nullptr;
    
    bool m_debug_mode = false;
};

} // namespace MS2000