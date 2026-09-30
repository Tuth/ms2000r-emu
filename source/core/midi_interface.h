#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <queue>
#include <thread>

namespace MS2000 {

// Forward declarations
class H8S2350Emulator;
class DSP56362Emulator;
class MS2000AudioInterface;

// MIDI Message types
enum class MIDIMessageType {
    NOTE_OFF = 0x80,
    NOTE_ON = 0x90,
    POLY_PRESSURE = 0xA0,
    CONTROL_CHANGE = 0xB0,
    PROGRAM_CHANGE = 0xC0,
    CHANNEL_PRESSURE = 0xD0,
    PITCH_BEND = 0xE0,
    SYSTEM_EXCLUSIVE = 0xF0,
    MIDI_CLOCK = 0xF8,
    MIDI_START = 0xFA,
    MIDI_CONTINUE = 0xFB,
    MIDI_STOP = 0xFC,
    ACTIVE_SENSING = 0xFE,
    RESET = 0xFF
};

// MIDI Message structure
struct MIDIMessage {
    MIDIMessageType type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
    uint32_t timestamp;
    std::vector<uint8_t> sysexData; // For System Exclusive messages
    
    MIDIMessage(MIDIMessageType msgType, uint8_t ch, uint8_t d1, uint8_t d2 = 0)
        : type(msgType), channel(ch), data1(d1), data2(d2), timestamp(0) {}
    
    MIDIMessage(MIDIMessageType msgType, const std::vector<uint8_t>& sysex)
        : type(msgType), channel(0), data1(0), data2(0), timestamp(0), sysexData(sysex) {}
};

// MIDI Device information
struct MIDIDeviceInfo {
    std::string name;
    std::string id;
    bool isInput;
    bool isOutput;
    bool isDefault;
};

// MIDI Interface class
class MIDIInterface {
public:
    MIDIInterface();
    ~MIDIInterface();
    
    // Initialization
    bool initialize(H8S2350Emulator* h8s, DSP56362Emulator* dsp, MS2000AudioInterface* audio);
    void shutdown();
    
    // Device Management
    std::vector<MIDIDeviceInfo> getAvailableDevices();
    bool openInputDevice(const std::string& deviceId);
    bool openOutputDevice(const std::string& deviceId);
    void closeInputDevice();
    void closeOutputDevice();
    
    // MIDI Message Handling
    void sendMessage(const MIDIMessage& message);
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    void sendProgramChange(uint8_t channel, uint8_t program);
    void sendPitchBend(uint8_t channel, uint16_t value);
    void sendSysEx(const std::vector<uint8_t>& data);
    
    // MIDI Clock and Sync
    void sendMIDIClock();
    void sendMIDIStart();
    void sendMIDIContinue();
    void sendMIDIStop();
    void setTempo(float bpm);
    float getTempo() const { return m_tempo; }
    
    // Callbacks
    void setNoteOnCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback);
    void setNoteOffCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback);
    void setControlChangeCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback);
    void setProgramChangeCallback(std::function<void(uint8_t, uint8_t)> callback);
    void setPitchBendCallback(std::function<void(uint8_t, uint16_t)> callback);
    void setSysExCallback(std::function<void(const std::vector<uint8_t>&)> callback);
    void setMIDIClockCallback(std::function<void()> callback);
    
    // MS2000-Specific MIDI
    void sendMS2000Preset(uint8_t bank, uint8_t program);
    void requestMS2000Preset(uint8_t bank, uint8_t program);
    void sendMS2000Parameter(uint8_t parameter, uint8_t value);
    void requestMS2000Parameter(uint8_t parameter);
    
    // Status
    bool isInitialized() const { return m_initialized; }
    bool isInputOpen() const { return m_inputOpen; }
    bool isOutputOpen() const { return m_outputOpen; }
    uint32_t getMessageCount() const { return m_messageCount; }
    
    // fw27.txt Phase 2: MIDI file replay support
    bool loadMIDIFile(const std::string& filename);
    bool loadSysExFile(const std::string& filename);
    bool startReplay();
    void stopReplay();
    bool isReplaying() const { return m_replayActive; }
    uint32_t getReplayPosition() const { return m_replayPosition; }
    uint32_t getReplayDuration() const { return m_replayDuration; }
    
    // fw27.txt Phase 2: Real instrument experience callbacks
    void setAudioOutputCallback(std::function<void(const int16_t*, int)> callback);
    void setLCDUpdateCallback(std::function<void(const std::string&)> callback);
    void setParameterChangeCallback(std::function<void(uint8_t, uint8_t)> callback);
    
    // fw27.txt Phase 2: Deterministic testing support
    struct ReplayStatistics {
        uint32_t eventsProcessed = 0;
        uint32_t noteOnCount = 0;
        uint32_t noteOffCount = 0;
        uint32_t sysExBytesProcessed = 0;
        uint32_t parameterUpdates = 0;
        uint32_t lcdUpdates = 0;
        bool deterministic = true;  // Passes hash comparison
    };
    
    const ReplayStatistics& getReplayStatistics() const { return m_replayStats; }
    void resetReplayStatistics() { m_replayStats = {}; }
    
private:
    // Core components
    H8S2350Emulator* m_h8s;
    DSP56362Emulator* m_dsp;
    MS2000AudioInterface* m_audio;
    bool m_initialized;
    
    // Device state
    bool m_inputOpen;
    bool m_outputOpen;
    std::string m_inputDeviceId;
    std::string m_outputDeviceId;
    
    // MIDI state
    float m_tempo;  // BPM
    uint32_t m_clockCount;
    uint32_t m_messageCount;
    
    // Message queues
    std::queue<MIDIMessage> m_inputQueue;
    std::queue<MIDIMessage> m_outputQueue;
    
    // Callbacks
    std::function<void(uint8_t, uint8_t, uint8_t)> m_noteOnCallback;
    std::function<void(uint8_t, uint8_t, uint8_t)> m_noteOffCallback;
    std::function<void(uint8_t, uint8_t, uint8_t)> m_controlChangeCallback;
    std::function<void(uint8_t, uint8_t)> m_programChangeCallback;
    std::function<void(uint8_t, uint16_t)> m_pitchBendCallback;
    std::function<void(const std::vector<uint8_t>&)> m_sysExCallback;
    std::function<void()> m_midiClockCallback;
    
    // Threading
    std::thread m_midiThread;
    std::atomic<bool> m_running;
    mutable std::mutex m_mutex;
    
    // fw27.txt Phase 2: Replay system state
    std::vector<MIDIMessage> m_replayEvents;
    std::atomic<bool> m_replayActive{false};
    uint32_t m_replayPosition = 0;
    uint32_t m_replayDuration = 0;
    size_t m_replayIndex = 0;
    std::chrono::steady_clock::time_point m_replayStartTime;
    ReplayStatistics m_replayStats;
    
    // fw27.txt Phase 2: Real instrument callbacks
    std::function<void(const int16_t*, int)> m_audioOutputCallback;
    std::function<void(const std::string&)> m_lcdUpdateCallback;
    std::function<void(uint8_t, uint8_t)> m_parameterChangeCallback;
    
    // Platform-specific data
    struct PlatformData;
    std::unique_ptr<PlatformData> m_platformData;
    
    // Internal methods
    void midiThreadFunction();
    void processInputMessage(const MIDIMessage& message);
    void processOutputMessage(const MIDIMessage& message);
    void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
    void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    void handleProgramChange(uint8_t channel, uint8_t program);
    void handlePitchBend(uint8_t channel, uint16_t value);
    void handleSysEx(const std::vector<uint8_t>& data);
    
    // Platform-specific methods
    bool initializePlatformMIDI();
    void shutdownPlatformMIDI();
    bool openPlatformInputDevice(const std::string& deviceId);
    bool openPlatformOutputDevice(const std::string& deviceId);
    void closePlatformInputDevice();
    void closePlatformOutputDevice();
    void sendPlatformMessage(const MIDIMessage& message);
    std::vector<MIDIDeviceInfo> getPlatformDevices();
};

} // namespace MS2000
