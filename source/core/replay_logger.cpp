#include "replay_logger.h"
#include <algorithm>
#include <cstring>

namespace MS2000 {

ReplayLogger::~ReplayLogger() {
    close();
}

bool ReplayLogger::open(const std::string& filename, Mode mode) {
    close();
    m_filename = filename;
    m_mode = mode;

    if (mode == Mode::RECORD) {
        m_file.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!m_file.is_open()) return false;
        return writeHeader();
    } else if (mode == Mode::REPLAY) {
        m_file.open(filename, std::ios::binary | std::ios::in);
        if (!m_file.is_open()) return false;
        if (!readHeader()) {
            close();
            return false;
        }
        m_queue_loaded = false;
        return true;
    }
    return false;
}

void ReplayLogger::close() {
    if (m_file.is_open()) {
        m_file.close();
    }
    m_mode = Mode::OFF;
    m_queue_loaded = false;
    while (!m_event_queue.empty()) m_event_queue.pop();
}

bool ReplayLogger::writeHeader() {
    uint32_t magic = MAGIC;
    uint32_t version = VERSION;
    uint64_t reserved = 0; // For future use

    m_file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    m_file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    m_file.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
    return m_file.good();
}

bool ReplayLogger::readHeader() {
    uint32_t magic, version;
    uint64_t reserved;
    
    m_file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    m_file.read(reinterpret_cast<char*>(&version), sizeof(version));
    m_file.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));

    if (!m_file.good()) return false;
    if (magic != MAGIC) return false;
    if (version != VERSION) return false;

    // Pre-load all events into queue for fast replay
    while (m_file.good() && !m_file.eof()) {
        if (!readNextEvent()) break;
    }
    m_queue_loaded = true;
    return true;
}

bool ReplayLogger::readNextEvent() {
    ReplayEventHeader header;
    m_file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!m_file.good() || m_file.eof()) return false;

    size_t payload_size = getPayloadSize(header.type, nullptr, 0);
    std::vector<uint8_t> payload(payload_size);
    if (payload_size > 0) {
        m_file.read(reinterpret_cast<char*>(payload.data()), payload_size);
        if (!m_file.good()) return false;
    }

    m_event_queue.emplace(header, std::move(payload));
    return true;
}

void ReplayLogger::writeEvent(const ReplayEventHeader& header, const void* payload, size_t payload_size) {
    m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (payload_size > 0 && payload) {
        m_file.write(reinterpret_cast<const char*>(payload), payload_size);
    }
    m_file.flush();
}

size_t ReplayLogger::getPayloadSize(ReplayEventType type, const void* /*data*/, size_t /*size*/) {
    switch (type) {
        case ReplayEventType::EV_IRQ_RAISE:
        case ReplayEventType::EV_IRQ_CLEAR:
            return 1; // vector
        case ReplayEventType::EV_MIDI_RX:
            return 2; // channel + data
        case ReplayEventType::EV_ADC:
            return 3; // channel + 2 bytes value
        case ReplayEventType::EV_DSP_HPI:
            // Variable size - in record we know it, in replay it's read from file
            // For peek we need to read the size from the file first
            return 0; // Will be handled specially
        default:
            return 0;
    }
}

// Recording functions
void ReplayLogger::recordIRQRaise(uint64_t cycle, uint8_t vector) {
    if (m_mode != Mode::RECORD) return;
    ReplayEventHeader header{cycle, ReplayEventType::EV_IRQ_RAISE};
    writeEvent(header, &vector, 1);
    m_event_count++;
}

void ReplayLogger::recordIRQClear(uint64_t cycle, uint8_t vector) {
    if (m_mode != Mode::RECORD) return;
    ReplayEventHeader header{cycle, ReplayEventType::EV_IRQ_CLEAR};
    writeEvent(header, &vector, 1);
    m_event_count++;
}

void ReplayLogger::recordMIDIRX(uint64_t cycle, uint8_t channel, uint8_t data) {
    if (m_mode != Mode::RECORD) return;
    ReplayEventHeader header{cycle, ReplayEventType::EV_MIDI_RX};
    uint8_t payload[2] = {channel, data};
    writeEvent(header, payload, 2);
    m_event_count++;
}

void ReplayLogger::recordADC(uint64_t cycle, uint8_t channel, uint16_t value) {
    if (m_mode != Mode::RECORD) return;
    ReplayEventHeader header{cycle, ReplayEventType::EV_ADC};
    uint8_t payload[3] = {channel, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0x03)};
    writeEvent(header, payload, 3);
    m_event_count++;
}

void ReplayLogger::recordDSPHPI(uint64_t cycle, uint8_t type, const void* data, size_t size) {
    if (m_mode != Mode::RECORD) return;
    ReplayEventHeader header{cycle, ReplayEventType::EV_DSP_HPI};
    // Write type byte + data
    uint8_t type_byte = type;
    writeEvent(header, &type_byte, 1);
    if (size > 0 && data) {
        m_file.write(reinterpret_cast<const char*>(data), size);
        m_file.flush();
    }
    m_event_count++;
}

// Replay functions
bool ReplayLogger::hasNextEvent() const {
    if (m_mode != Mode::REPLAY) return false;
    return !m_event_queue.empty();
}

std::optional<ReplayEventHeader> ReplayLogger::peekNextEvent() const {
    if (m_mode != Mode::REPLAY || m_event_queue.empty()) return std::nullopt;
    return m_event_queue.front().first;
}

bool ReplayLogger::popNextEvent() {
    if (m_mode != Mode::REPLAY || m_event_queue.empty()) return false;
    
    const auto& front = m_event_queue.front();
    const ReplayEventHeader& header = front.first;
    const std::vector<uint8_t>& payload = front.second;

    // Decode payload
    switch (header.type) {
        case ReplayEventType::EV_IRQ_RAISE:
        case ReplayEventType::EV_IRQ_CLEAR:
            if (payload.size() >= 1) m_last_vector = payload[0];
            break;
        case ReplayEventType::EV_MIDI_RX:
            if (payload.size() >= 2) {
                m_last_midi_channel = payload[0];
                m_last_midi_data = payload[1];
            }
            break;
        case ReplayEventType::EV_ADC:
            if (payload.size() >= 3) {
                m_last_adc_channel = payload[0];
                m_last_adc_value = static_cast<uint16_t>(payload[1]) | (static_cast<uint16_t>(payload[2] & 0x03) << 8);
            }
            break;
        case ReplayEventType::EV_DSP_HPI:
            if (payload.size() >= 1) {
                m_last_dsp_type = payload[0];
                if (payload.size() > 1) {
                    m_last_dsp_data.assign(payload.begin() + 1, payload.end());
                } else {
                    m_last_dsp_data.clear();
                }
            }
            break;
        default:
            break;
    }

    m_event_queue.pop();
    m_replayed_count++;
    return true;
}

void ReplayLogger::syncToNextEvent(uint64_t current_cycle) {
    if (m_mode != Mode::REPLAY) return;
    
    while (hasNextEvent()) {
        auto opt = peekNextEvent();
        if (!opt) break;
        if (opt->cycle <= current_cycle) {
            // Event is due now, will be consumed by caller
            break;
        }
        // Next event is in the future, stop syncing
        break;
    }
}

} // namespace MS2000