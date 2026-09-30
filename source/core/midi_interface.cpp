#include "midi_interface.h"

namespace MS2000 {

// PlatformData struct definition
struct MIDIInterface::PlatformData {
    // Platform-specific data will be added later
    bool initialized;
    
    PlatformData() : initialized(false) {}
};

MIDIInterface::MIDIInterface()
    : m_h8s(nullptr)
    , m_dsp(nullptr)
    , m_audio(nullptr)
    , m_initialized(false)
    , m_inputOpen(false)
    , m_outputOpen(false)
    , m_tempo(120.0f)
    , m_clockCount(0)
    , m_messageCount(0)
    , m_running(false)
    , m_platformData(std::make_unique<PlatformData>()) {
}

MIDIInterface::~MIDIInterface() {
    shutdown();
}

bool MIDIInterface::initialize(H8S2350Emulator* h8s, DSP56362Emulator* dsp, MS2000AudioInterface* audio) {
    m_h8s = h8s;
    m_dsp = dsp;
    m_audio = audio;
    m_initialized = true;
    return true;
}

void MIDIInterface::shutdown() {
    m_initialized = false;
    m_running = false;
}

std::vector<MIDIDeviceInfo> MIDIInterface::getAvailableDevices() {
    return std::vector<MIDIDeviceInfo>();
}

bool MIDIInterface::openInputDevice(const std::string& deviceId) {
    m_inputOpen = true;
    m_inputDeviceId = deviceId;
    return true;
}

bool MIDIInterface::openOutputDevice(const std::string& deviceId) {
    m_outputOpen = true;
    m_outputDeviceId = deviceId;
    return true;
}

void MIDIInterface::closeInputDevice() {
    m_inputOpen = false;
}

void MIDIInterface::closeOutputDevice() {
    m_outputOpen = false;
}

void MIDIInterface::sendMessage(const MIDIMessage& message) {
    m_messageCount++;
}

void MIDIInterface::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    MIDIMessage msg(MIDIMessageType::NOTE_ON, channel, note, velocity);
    sendMessage(msg);
}

void MIDIInterface::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    MIDIMessage msg(MIDIMessageType::NOTE_OFF, channel, note, velocity);
    sendMessage(msg);
}

void MIDIInterface::sendControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
    MIDIMessage msg(MIDIMessageType::CONTROL_CHANGE, channel, controller, value);
    sendMessage(msg);
}

void MIDIInterface::sendProgramChange(uint8_t channel, uint8_t program) {
    MIDIMessage msg(MIDIMessageType::PROGRAM_CHANGE, channel, program);
    sendMessage(msg);
}

void MIDIInterface::sendPitchBend(uint8_t channel, uint16_t value) {
    MIDIMessage msg(MIDIMessageType::PITCH_BEND, channel, value & 0x7F, (value >> 7) & 0x7F);
    sendMessage(msg);
}

void MIDIInterface::sendSysEx(const std::vector<uint8_t>& data) {
    MIDIMessage msg(MIDIMessageType::SYSTEM_EXCLUSIVE, data);
    sendMessage(msg);
}

void MIDIInterface::sendMIDIClock() {
    MIDIMessage msg(MIDIMessageType::MIDI_CLOCK, 0, 0);
    sendMessage(msg);
}

void MIDIInterface::sendMIDIStart() {
    MIDIMessage msg(MIDIMessageType::MIDI_START, 0, 0);
    sendMessage(msg);
}

void MIDIInterface::sendMIDIContinue() {
    MIDIMessage msg(MIDIMessageType::MIDI_CONTINUE, 0, 0);
    sendMessage(msg);
}

void MIDIInterface::sendMIDIStop() {
    MIDIMessage msg(MIDIMessageType::MIDI_STOP, 0, 0);
    sendMessage(msg);
}

void MIDIInterface::setTempo(float bpm) {
    m_tempo = bpm;
}

void MIDIInterface::setNoteOnCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback) {
    m_noteOnCallback = callback;
}

void MIDIInterface::setNoteOffCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback) {
    m_noteOffCallback = callback;
}

void MIDIInterface::setControlChangeCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback) {
    m_controlChangeCallback = callback;
}

void MIDIInterface::setProgramChangeCallback(std::function<void(uint8_t, uint8_t)> callback) {
    m_programChangeCallback = callback;
}

void MIDIInterface::setPitchBendCallback(std::function<void(uint8_t, uint16_t)> callback) {
    m_pitchBendCallback = callback;
}

void MIDIInterface::setSysExCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    m_sysExCallback = callback;
}

void MIDIInterface::setMIDIClockCallback(std::function<void()> callback) {
    m_midiClockCallback = callback;
}

void MIDIInterface::sendMS2000Preset(uint8_t bank, uint8_t program) {
    // TODO: Implement MS2000-specific SysEx
}

void MIDIInterface::requestMS2000Preset(uint8_t bank, uint8_t program) {
    // TODO: Implement MS2000-specific SysEx
}

void MIDIInterface::sendMS2000Parameter(uint8_t parameter, uint8_t value) {
    // TODO: Implement MS2000-specific SysEx
}

void MIDIInterface::requestMS2000Parameter(uint8_t parameter) {
    // TODO: Implement MS2000-specific SysEx
}

} // namespace MS2000
