#pragma once
// Host audio out (WASAPI, shared mode) and MIDI in (winmm) for the thin GUI. Windows only.
// Nothing here models MS2000 hardware: it is the bridge between the emulated board's
// outputs/inputs and the PC's devices.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace MS2000 {

struct HostDevice { std::wstring id; std::string name; };

// WASAPI render. The emulator's audio is 48 kHz stereo; the stream asks Windows to convert to the
// device mix format (AUTOCONVERTPCM + SRC_DEFAULT_QUALITY) when that is not 48 kHz float.
class HostAudioOut {
public:
    // Fill `frames` interleaved stereo float frames in [-1, 1]; return how many were real audio
    // (the rest must be zero-filled by the callee) - the shortfall is counted as underrun frames.
    using Pull = std::function<uint32_t(float* lr, uint32_t frames)>;

    static std::vector<HostDevice> list();            // render endpoints, default first
    bool open(const std::wstring& deviceId, Pull pull, uint32_t bufferMs = 20);
    void close();
    bool isOpen() const { return m_thread.joinable(); }
    std::string lastError() const { std::lock_guard<std::mutex> l(m_errMx); return m_err; }
    uint64_t underrunFrames() const { return m_underrun; }
    uint32_t deviceRate() const { return m_devRate; }
    bool dead() const { return m_dead.load(); }        // the stream ended on its own (HOST-AUDIO-STALL)
    ~HostAudioOut() { close(); }
private:
    void run(std::wstring id);
    void fail(const std::string& e) { std::lock_guard<std::mutex> l(m_errMx); m_err = e; }
    Pull m_pull;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_dead{false};
    std::atomic<uint64_t> m_underrun{0};
    std::atomic<uint32_t> m_devRate{0};
    uint32_t m_bufferMs = 20;
    mutable std::mutex m_errMx;
    std::string m_err;
};

// WASAPI capture (AUDIO-IN round): 48 kHz stereo float frames, handed on as they arrive.
class HostAudioIn {
public:
    using Sink = std::function<void(const float* lr, uint32_t frames)>;
    static std::vector<HostDevice> list();            // capture endpoints, default first
    bool open(const std::wstring& deviceId, Sink sink);
    void close();
    bool isOpen() const { return m_thread.joinable(); }
    std::string lastError() const { std::lock_guard<std::mutex> l(m_errMx); return m_err; }
    uint64_t frames() const { return m_frames; }
    bool dead() const { return m_dead.load(); }        // the stream ended on its own (HOST-AUDIO-STALL)
    ~HostAudioIn() { close(); }
private:
    void run(std::wstring id);
    Sink m_sink;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_dead{false};
    std::atomic<uint64_t> m_frames{0};
    mutable std::mutex m_errMx;
    std::string m_err;
};

// winmm MIDI IN. Short messages and SysEx are handed on as raw bytes, in order.
class HostMidiIn {
public:
    using Sink = std::function<void(const uint8_t* data, size_t n)>;
    static std::vector<std::string> list();
    bool open(unsigned index, Sink sink);
    void close();
    bool isOpen() const { return m_handle != nullptr; }
    uint64_t bytesIn() const { return m_bytes; }
    ~HostMidiIn() { close(); }
    // called from the winmm callback thread
    void onShort(uint32_t msg);
    void onLong(void* hdr);
private:
    void* m_handle = nullptr;       // HMIDIIN
    Sink m_sink;
    std::atomic<uint64_t> m_bytes{0};
    std::vector<std::vector<uint8_t>> m_sysexBufs;
    std::vector<void*> m_hdrs;      // MIDIHDR*
    std::atomic<bool> m_closing{false};
};

// winmm MIDI OUT (2026-09-27): the MS2000's SCI1 byte stream (TXD1 = MIDI OUT, KOD-A30411) regrouped into
// complete messages - running status, one- and two-byte system messages, realtime bytes anywhere, SysEx
// F0..F7 as one long message - and sent to a host port. byte() is called from the emulation thread.
class HostMidiOut {
public:
    static std::vector<std::string> list();
    bool open(unsigned index);
    void close();
    bool isOpen() const { return m_handle != nullptr; }
    void byte(uint8_t b);
    uint64_t bytesOut() const { return m_bytes; }
    ~HostMidiOut() { close(); }
private:
    void sendShort(const uint8_t* m, size_t n);
    void sendSysex();
    void reapDone();
    std::mutex m_mx;
    void* m_handle = nullptr;       // HMIDIOUT
    std::atomic<uint64_t> m_bytes{0};
    uint8_t m_status = 0, m_msg[3] = {}; size_t m_have = 0, m_need = 0;
    bool m_inSysex = false;
    std::vector<uint8_t> m_sysex;
    std::vector<void*> m_pending;   // MIDIHDR* still owned by the driver
};

} // namespace MS2000
