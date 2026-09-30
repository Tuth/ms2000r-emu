// fw14.txt: Panel Recorder/Replay system for stream capture and debugging
// fw15.txt: Enhanced with PRNG seed control and deterministic state
#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <fstream>
#include <functional>
#include "deterministic_state.h"

struct PanelCapture {
    std::chrono::steady_clock::time_point timestamp;
    uint8_t data;
    bool isHPI;      // true=HPI, false=SCI
    int sciId;       // SCI channel (0-2), ignored if isHPI=true
    
    PanelCapture() : data(0), isHPI(false), sciId(0) {}
    PanelCapture(uint8_t b, bool hpi = false, int sci = 0) 
        : timestamp(std::chrono::steady_clock::now()), data(b), isHPI(hpi), sciId(sci) {}
};

class PanelRecorder {
public:
    // fw15.txt: Start recording with deterministic state capture
    void startRecording(DeterministicState* deterministicState = nullptr) {
        m_recording = true;
        m_captures.clear();
        m_startTime = std::chrono::steady_clock::now();
        
        // fw15.txt: Capture initial deterministic state
        if (deterministicState) {
            m_initialState = deterministicState->serialize();
            m_hasDeterministicState = true;
        } else {
            m_hasDeterministicState = false;
        }
    }
    
    void stopRecording() {
        m_recording = false;
    }
    
    void captureData(uint8_t data, bool isHPI = false, int sciId = 0) {
        if (!m_recording) return;
        
        m_captures.emplace_back(data, isHPI, sciId);
        
        // Limit buffer size to prevent memory issues
        if (m_captures.size() > 100000) {
            m_captures.erase(m_captures.begin(), m_captures.begin() + 10000);
        }
    }
    
    bool isRecording() const { return m_recording; }
    size_t getCaptureCount() const { return m_captures.size(); }
    
    // Save capture to .mpcap file (binary format with header)
    bool saveTo(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file) return false;
        
        // fw15.txt: Enhanced file header with deterministic state
        file.write("MPCAP", 5);
        uint32_t version = 2;  // fw15.txt: Bumped version for deterministic state support
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        
        // fw15.txt: Write deterministic state flag and data
        file.write(reinterpret_cast<const char*>(&m_hasDeterministicState), sizeof(m_hasDeterministicState));
        if (m_hasDeterministicState) {
            file.write(reinterpret_cast<const char*>(&m_initialState), sizeof(m_initialState));
        }
        
        uint32_t count = static_cast<uint32_t>(m_captures.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        // Base timestamp (to save space, store relative times)
        auto baseTime = m_startTime.time_since_epoch().count();
        file.write(reinterpret_cast<const char*>(&baseTime), sizeof(baseTime));
        
        // Capture data
        for (const auto& cap : m_captures) {
            auto relativeTime = (cap.timestamp - m_startTime).count();
            file.write(reinterpret_cast<const char*>(&relativeTime), sizeof(relativeTime));
            file.write(reinterpret_cast<const char*>(&cap.data), sizeof(cap.data));
            file.write(reinterpret_cast<const char*>(&cap.isHPI), sizeof(cap.isHPI));
            file.write(reinterpret_cast<const char*>(&cap.sciId), sizeof(cap.sciId));
        }
        
        return file.good();
    }
    
    // Load capture from .mpcap file
    bool loadFrom(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        
        // Check header
        char header[6] = {0};
        file.read(header, 5);
        if (std::string(header) != "MPCAP") return false;
        
        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version < 1 || version > 2) return false;  // fw15.txt: Support both v1 and v2
        
        // fw15.txt: Load deterministic state if present (v2 format)
        if (version >= 2) {
            file.read(reinterpret_cast<char*>(&m_hasDeterministicState), sizeof(m_hasDeterministicState));
            if (m_hasDeterministicState) {
                file.read(reinterpret_cast<char*>(&m_initialState), sizeof(m_initialState));
            }
        } else {
            m_hasDeterministicState = false;
        }
        
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        decltype(m_startTime.time_since_epoch().count()) baseTime;
        file.read(reinterpret_cast<char*>(&baseTime), sizeof(baseTime));
        
        m_startTime = std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(baseTime));
        
        m_captures.clear();
        m_captures.reserve(count);
        
        for (uint32_t i = 0; i < count; i++) {
            PanelCapture cap;
            
            decltype((cap.timestamp - m_startTime).count()) relativeTime;
            file.read(reinterpret_cast<char*>(&relativeTime), sizeof(relativeTime));
            cap.timestamp = m_startTime + std::chrono::steady_clock::duration(relativeTime);
            
            file.read(reinterpret_cast<char*>(&cap.data), sizeof(cap.data));
            file.read(reinterpret_cast<char*>(&cap.isHPI), sizeof(cap.isHPI));
            file.read(reinterpret_cast<char*>(&cap.sciId), sizeof(cap.sciId));
            
            m_captures.push_back(cap);
        }
        
        return file.good();
    }
    
    const std::vector<PanelCapture>& getCaptures() const { return m_captures; }
    
    // fw15.txt: Deterministic state access
    bool hasDeterministicState() const { return m_hasDeterministicState; }
    const DeterministicState::SerializedState& getInitialState() const { return m_initialState; }

private:
    bool m_recording = false;
    std::vector<PanelCapture> m_captures;
    std::chrono::steady_clock::time_point m_startTime;
    
    // fw15.txt: Deterministic state for replay
    bool m_hasDeterministicState = false;
    DeterministicState::SerializedState m_initialState;
};

class PanelReplayer {
public:
    PanelReplayer() : m_currentIndex(0), m_playing(false) {}
    
    void loadCapture(const std::vector<PanelCapture>& captures) {
        m_captures = captures;
        m_currentIndex = 0;
        m_playing = false;
        if (!m_captures.empty()) {
            m_replayStartTime = std::chrono::steady_clock::now();
            m_captureStartTime = m_captures[0].timestamp;
        }
    }
    
    void startReplay() {
        if (m_captures.empty()) return;
        m_playing = true;
        m_replayStartTime = std::chrono::steady_clock::now();
        m_currentIndex = 0;
    }
    
    void stopReplay() {
        m_playing = false;
    }
    
    // Call this periodically to get next data to replay
    // Returns true if data is available, false if no more data or not playing
    bool getNextReplayData(uint8_t& data, bool& isHPI, int& sciId) {
        if (!m_playing || m_currentIndex >= m_captures.size()) {
            if (m_currentIndex >= m_captures.size()) {
                m_playing = false; // End of replay
            }
            return false;
        }
        
        const auto& cap = m_captures[m_currentIndex];
        auto now = std::chrono::steady_clock::now();
        auto elapsedReplay = now - m_replayStartTime;
        auto elapsedOriginal = cap.timestamp - m_captureStartTime;
        
        // Check if it's time to replay this capture
        if (elapsedReplay >= elapsedOriginal) {
            data = cap.data;
            isHPI = cap.isHPI;
            sciId = cap.sciId;
            m_currentIndex++;
            return true;
        }
        
        return false;
    }
    
    bool isPlaying() const { return m_playing; }
    size_t getCurrentIndex() const { return m_currentIndex; }
    size_t getTotalCount() const { return m_captures.size(); }
    
    float getProgress() const {
        if (m_captures.empty()) return 0.0f;
        return static_cast<float>(m_currentIndex) / static_cast<float>(m_captures.size());
    }

private:
    std::vector<PanelCapture> m_captures;
    size_t m_currentIndex;
    bool m_playing;
    std::chrono::steady_clock::time_point m_replayStartTime;
    std::chrono::steady_clock::time_point m_captureStartTime;
};