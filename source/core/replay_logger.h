#pragma once

#include <cstdint>
#include <fstream>
#include <vector>
#include <queue>
#include <string>
#include <optional>

namespace MS2000 {

// Forward declaration
class H8S2350Emulator;

// Replay event types
enum class ReplayEventType : uint8_t {
    EV_NONE = 0,
    EV_IRQ_RAISE = 0x01,       // [1 byte: vector]
    EV_IRQ_CLEAR = 0x02,       // [1 byte: vector]
    EV_MIDI_RX = 0x03,         // [1 byte: channel] [1 byte: data]
    EV_ADC = 0x04,             // [1 byte: channel] [2 bytes: value (10-bit)]
    EV_DSP_HPI = 0x05,         // [1 byte: type] [variable data]
};

// Event header (8 bytes cycle + 1 byte type = 9 bytes minimum)
struct ReplayEventHeader {
    uint64_t cycle;           // Emulator cycle timestamp
    ReplayEventType type;     // Event type
};

class ReplayLogger {
public:
    enum class Mode {
        OFF,
        RECORD,
        REPLAY
    };

    ReplayLogger() = default;
    ~ReplayLogger();

    // Open file for recording or replay
    bool open(const std::string& filename, Mode mode);
    void close();

    bool isOpen() const { return m_file.is_open(); }
    Mode getMode() const { return m_mode; }

    // Recording functions
    void recordIRQRaise(uint64_t cycle, uint8_t vector);
    void recordIRQClear(uint64_t cycle, uint8_t vector);
    void recordMIDIRX(uint64_t cycle, uint8_t channel, uint8_t data);
    void recordADC(uint64_t cycle, uint8_t channel, uint16_t value); // 10-bit value
    void recordDSPHPI(uint64_t cycle, uint8_t type, const void* data, size_t size);

    // Replay functions
    bool hasNextEvent() const;
    std::optional<ReplayEventHeader> peekNextEvent() const;
    bool popNextEvent();

    // Get event data after peek/pop
    uint8_t getIRQVector() const { return m_last_vector; }
    uint8_t getMIDIChannel() const { return m_last_midi_channel; }
    uint8_t getMIDIData() const { return m_last_midi_data; }
    uint8_t getADCChannel() const { return m_last_adc_channel; }
    uint16_t getADCValue() const { return m_last_adc_value; }
    const std::vector<uint8_t>& getDSPHPIData() const { return m_last_dsp_data; }
    uint8_t getDSPHPITType() const { return m_last_dsp_type; }

    // Sync: wait until next event cycle (call from main loop)
    void syncToNextEvent(uint64_t current_cycle);

    // Stats
    uint64_t getRecordedEventCount() const { return m_event_count; }
    uint64_t getReplayedEventCount() const { return m_replayed_count; }

private:
    // File format:
    // [4 bytes magic: 'MS2R'] [4 bytes version] [events...]
    // Each event: [8 bytes cycle] [1 byte type] [payload...]

    static constexpr uint32_t MAGIC = 0x4D533252; // 'MS2R'
    static constexpr uint32_t VERSION = 1;

    Mode m_mode = Mode::OFF;
    std::fstream m_file;
    std::string m_filename;

    // Replay buffer
    std::queue<std::pair<ReplayEventHeader, std::vector<uint8_t>>> m_event_queue;
    bool m_queue_loaded = false;

    // Last decoded event data
    uint8_t m_last_vector = 0;
    uint8_t m_last_midi_channel = 0;
    uint8_t m_last_midi_data = 0;
    uint8_t m_last_adc_channel = 0;
    uint16_t m_last_adc_value = 0;
    uint8_t m_last_dsp_type = 0;
    std::vector<uint8_t> m_last_dsp_data;

    uint64_t m_event_count = 0;
    uint64_t m_replayed_count = 0;

    // Internal helpers
    bool writeHeader();
    bool readHeader();
    bool readNextEvent();
    void writeEvent(const ReplayEventHeader& header, const void* payload, size_t payload_size);
    size_t getPayloadSize(ReplayEventType type, const void* data, size_t size);
};

} // namespace MS2000