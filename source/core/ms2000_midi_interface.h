#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <queue>
#include "midi_interface.h" // Use existing MIDIMessage

namespace MS2000 {

// MS2000 MIDI Interface - SCI0 based, 31.25 kbps
// Based on hardware specification: MCU SCI ↔ MIDI IN/OUT/THRU

// MIDI callbacks for host communication
using MIDIOutputCallback = std::function<void(const uint8_t* data, size_t length)>;
using MIDIInputCallback = std::function<void(const MIDIMessage& message)>;

class MS2000MIDIInterface {
public:
    MS2000MIDIInterface();
    
    // Host MIDI callbacks
    void setMIDIOutputCallback(MIDIOutputCallback callback) { m_midi_output_callback = callback; }
    void setMIDIInputCallback(MIDIInputCallback callback) { m_midi_input_callback = callback; }
    
    // SCI0 Register Interface (called by H8S emulator)
    void writeSMR(uint8_t value);   // Serial Mode Register
    void writeBRR(uint8_t value);   // Bit Rate Register  
    void writeSCR(uint8_t value);   // Serial Control Register
    void writeTDR(uint8_t value);   // Transmit Data Register
    void writeSCMR(uint8_t value);  // Serial Control Mode Register
    
    uint8_t readSMR() const;        // Serial Mode Register
    uint8_t readBRR() const;        // Bit Rate Register
    uint8_t readSCR() const;        // Serial Control Register
    uint8_t readSSR() const;        // Serial Status Register
    uint8_t readRDR();              // Receive Data Register
    uint8_t readSCMR() const;       // Serial Control Mode Register
    
    // Host MIDI input (from external MIDI devices)
    void receiveMIDIData(const uint8_t* data, size_t length);
    
    // Process timing - call periodically for UART simulation
    void processTiming(uint32_t current_time_ms);
    
    // Debug info
    void setDebugMode(bool enabled) { m_debug_mode = enabled; }
    
    // Statistics
    uint32_t getTXByteCount() const { return m_tx_byte_count; }
    uint32_t getRXByteCount() const { return m_rx_byte_count; }

private:
    // SCI0 Registers (based on YAML spec)
    uint8_t m_SMR = 0x00;   // Serial Mode Register
    uint8_t m_BRR = 0xFF;   // Bit Rate Register  
    uint8_t m_SCR = 0x00;   // Serial Control Register
    uint8_t m_SSR = 0x84;   // Serial Status Register (TDRE=1, TEND=1)
    uint8_t m_SCMR = 0xF2;  // Serial Control Mode Register
    
    // UART simulation
    std::queue<uint8_t> m_tx_queue;     // Transmit queue
    std::queue<uint8_t> m_rx_queue;     // Receive queue
    
    uint32_t m_last_tx_time = 0;        // Last transmit time
    uint32_t m_last_rx_time = 0;        // Last receive time
    
    // MIDI state
    std::vector<uint8_t> m_current_message; // Current incoming MIDI message
    uint8_t m_running_status = 0;            // MIDI running status
    
    // Statistics
    uint32_t m_tx_byte_count = 0;
    uint32_t m_rx_byte_count = 0;
    
    // Callbacks
    MIDIOutputCallback m_midi_output_callback = nullptr;
    MIDIInputCallback m_midi_input_callback = nullptr;
    
    bool m_debug_mode = false;
    
    // Timing constants (31.25 kbps = 320μs per byte)
    static constexpr uint32_t MIDI_BYTE_TIME_US = 320;  // 320μs per byte at 31.25 kbps
    static constexpr uint32_t MIDI_BYTE_TIME_MS = 1;    // ~1ms per byte (rounded up)
    
    // Internal helpers
    void processTransmit(uint32_t current_time_ms);
    void processReceive(uint32_t current_time_ms);
    void updateSSRFlags();
    bool isTransmitterEnabled() const { return (m_SCR & 0x20) != 0; } // TE bit
    bool isReceiverEnabled() const { return (m_SCR & 0x10) != 0; }    // RE bit
    bool isTXInterruptEnabled() const { return (m_SCR & 0x80) != 0; } // TIE bit
    bool isRXInterruptEnabled() const { return (m_SCR & 0x40) != 0; } // RIE bit
    
    // MIDI message parsing
    void parseMIDIMessage(uint8_t byte);
    void processMIDIMessage();
    bool isStatusByte(uint8_t byte) const { return (byte & 0x80) != 0; }
    bool isSystemCommon(uint8_t byte) const { return (byte >= 0xF0 && byte <= 0xF7); }
    bool isSystemRealTime(uint8_t byte) const { return (byte >= 0xF8); }
    size_t getExpectedDataBytes(uint8_t status) const;
};

} // namespace MS2000