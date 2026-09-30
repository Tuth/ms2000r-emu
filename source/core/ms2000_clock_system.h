#pragma once

#include <cstdint>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <array>
#include <vector>
#include <cstring>

namespace MS2000 {

// MS2000 Hardware Clock System
// Implements exact clock frequencies as per Service Manual
class MS2000ClockSystem {
public:
    // Hardware-accurate clock frequencies (from Service Manual)
    static constexpr uint32_t MASTER_CLOCK_FREQ = 12288000;    // 12.288 MHz crystal
    static constexpr uint32_t H8S_CLOCK_FREQ = 10000000;       // 10.000 MHz ceramic oscillator (CSTCC10)
    static constexpr uint32_t DSP_CLOCK_FREQ = 80000000;       // 80.000 MHz (derived from master)
    static constexpr uint32_t AUDIO_CLOCK_FREQ = 48000;        // 48.000 kHz (master/256)
    static constexpr uint32_t RTC_CLOCK_FREQ = 32768;          // 32.768 kHz RTC crystal (IC25-A on panel)
    
    // Clock dividers
    static constexpr uint32_t MASTER_TO_AUDIO_DIVIDER = 256;   // 12.288MHz / 256 = 48kHz
    static constexpr uint32_t MASTER_TO_DSP_DIVIDER = 1;       // Direct connection
    static constexpr uint32_t MASTER_TO_H8S_DIVIDER = 1;       // Direct connection
    static constexpr uint32_t MASTER_TO_RTC_DIVIDER = 375;     // 12.288MHz / 375 = 32768 Hz
    
    // Pre-calculated clock ratios to avoid division by zero
    static constexpr uint32_t H8S_CLOCK_RATIO = 1;  // H8S runs at same rate as master in this implementation
    static constexpr uint32_t DSP_CLOCK_RATIO = 8;  // DSP runs 8x faster than master (80MHz vs 10MHz effective)
    static constexpr uint32_t RTC_CLOCK_RATIO = 1;  // RTC runs at 32.768 kHz
    
    // Frame sync timing
    static constexpr uint32_t FRAME_SYNC_PERIOD = 256;         // 256 master clock cycles per audio frame
    static constexpr uint32_t BIT_CLOCK_PERIOD = 4;            // 4 master clock cycles per bit (24-bit audio)
    
    MS2000ClockSystem();
    ~MS2000ClockSystem();
    
    // Initialization and Control
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Clock Generation
    void startClockGeneration();
    void stopClockGeneration();
    bool isClockRunning() const { return m_clockRunning; }
    
    // Clock Access
    uint64_t getMasterClockCount() const { return m_masterClockCount; }
    uint64_t getH8SClockCount() const { return m_h8sClockCount; }
    uint64_t getDSPClockCount() const { return m_dspClockCount; }
    uint64_t getAudioClockCount() const { return m_audioClockCount; }
    uint64_t getRTCClockCount() const { return m_rtcClockCount; }
    uint32_t getRTCSecondCount() const { return m_rtcSecondCount; }
    
    // Frame Sync Generation
    bool isFrameSyncActive() const { return m_frameSyncActive; }
    uint32_t getFrameSyncCount() const { return m_frameSyncCount; }
    uint32_t getBitClockCount() const { return m_bitClockCount; }
    
    // Clock Domain Synchronization
    void synchronizeClockDomains();
    bool areClocksSynchronized() const { return m_clocksSynchronized; }
    
    // Timing Information
    double getMasterClockTime() const;      // Time in seconds since start
    double getH8SClockTime() const;         // Time in seconds since start
    double getDSPClockTime() const;         // Time in seconds since start
    double getAudioClockTime() const;       // Time in seconds since start
    
    // Audio Frame Timing
    uint32_t getAudioFrameNumber() const { return m_audioFrameNumber; }
    double getAudioFrameTime() const;       // Time of current audio frame
    double getNextAudioFrameTime() const;   // Time of next audio frame
    
    // Callbacks for clock events
    void setFrameSyncCallback(std::function<void(bool)> callback);
    void setBitClockCallback(std::function<void(bool)> callback);
    void setAudioFrameCallback(std::function<void(uint32_t)> callback);
    
    // Clock Drift Detection
    double getClockDrift() const { return m_clockDrift; }
    bool isClockDriftDetected() const { return m_clockDriftDetected; }
    
    // Performance Monitoring
    double getClockAccuracy() const { return m_clockAccuracy; }
    uint64_t getTotalMasterCycles() const { return m_totalMasterCycles; }
    
private:
    // Clock State
    std::atomic<uint64_t> m_masterClockCount{0};
    std::atomic<uint64_t> m_h8sClockCount{0};
    std::atomic<uint64_t> m_dspClockCount{0};
    std::atomic<uint64_t> m_audioClockCount{0};
    std::atomic<uint64_t> m_rtcClockCount{0};               // 32.768 kHz RTC clock counter
    
    // Frame Sync State
    std::atomic<bool> m_frameSyncActive{false};
    std::atomic<uint32_t> m_frameSyncCount{0};
    std::atomic<uint32_t> m_bitClockCount{0};
    std::atomic<uint32_t> m_audioFrameNumber{0};
    std::atomic<uint32_t> m_rtcSecondCount{0};              // 1 Hz RTC second counter
    
    // Timing State
    std::chrono::steady_clock::time_point m_startTime;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    std::chrono::steady_clock::time_point m_lastBitTime;
    
    // Control State
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_clockRunning{false};
    std::atomic<bool> m_clocksSynchronized{false};
    
    // Clock Drift Detection
    std::atomic<double> m_clockDrift{0.0};
    std::atomic<bool> m_clockDriftDetected{false};
    std::atomic<double> m_clockAccuracy{1.0};
    
    // Performance Monitoring
    std::atomic<uint64_t> m_totalMasterCycles{0};
    
    // Threading
    std::thread m_clockThread;
    std::atomic<bool> m_threadRunning{false};
    mutable std::mutex m_clockMutex;
    
    // Callbacks
    std::function<void(bool)> m_frameSyncCallback;
    std::function<void(bool)> m_bitClockCallback;
    std::function<void(uint32_t)> m_audioFrameCallback;
    
    // Internal Methods
    void clockThreadFunction();
    void generateMasterClock();
    void generateH8SClock();
    void generateDSPClock();
    void generateAudioClock();
    void generateRTCClock();
    void generateFrameSync();
    void generateBitClock();
    
    void updateClockDrift();
    void checkClockSynchronization();
    
    // Timing Helpers
    double getElapsedTime() const;
    uint64_t calculateExpectedCycles(double elapsedTime, uint32_t frequency) const;
    double calculateClockAccuracy(uint64_t actual, uint64_t expected) const;
};

// Clock Domain Manager
// Manages synchronization between different clock domains
class ClockDomainManager {
public:
    enum class ClockDomain {
        MASTER,     // 12.288 MHz
        H8S,        // 10.000 MHz
        DSP,        // 80.000 MHz
        AUDIO,      // 48.000 kHz
        RTC         // 32.768 kHz (panel CPU real-time clock)
    };
    
    ClockDomainManager(MS2000ClockSystem* clockSystem);
    ~ClockDomainManager();
    
    // Domain Synchronization
    void synchronizeDomain(ClockDomain domain);
    bool isDomainSynchronized(ClockDomain domain) const;
    
    // Cross-Domain Communication
    template<typename T>
    T readFromDomain(ClockDomain source, ClockDomain target, const T& data);
    
    template<typename T>
    void writeToDomain(ClockDomain source, ClockDomain target, const T& data);
    
    // Timing Validation
    bool validateTiming(ClockDomain domain, uint64_t cycles);
    double getDomainTime(ClockDomain domain) const;
    uint32_t getDomainFrequency(ClockDomain domain) const;
    
private:
    MS2000ClockSystem* m_clockSystem;
    std::atomic<bool> m_domainSync[5];  // One for each domain (MASTER, H8S, DSP, AUDIO, RTC)
    
    // Cross-domain buffers
    struct CrossDomainBuffer {
        std::mutex mutex;
        std::vector<uint8_t> data;
        uint64_t timestamp;
        ClockDomain source;
    };
    
    std::array<CrossDomainBuffer, 16> m_crossDomainBuffers;  // 4x4 matrix for domain pairs
};

// Clock Event Logger
// Logs clock events for debugging and analysis
class ClockEventLogger {
public:
    enum class EventType {
        FRAME_SYNC_RISING,
        FRAME_SYNC_FALLING,
        BIT_CLOCK_RISING,
        BIT_CLOCK_FALLING,
        AUDIO_FRAME_START,
        AUDIO_FRAME_END,
        CLOCK_DRIFT_DETECTED,
        DOMAIN_SYNC_LOST
    };
    
    struct ClockEvent {
        EventType type;
        uint64_t timestamp;
        uint64_t masterClockCount;
        uint32_t audioFrameNumber;
        double clockTime;
    };
    
    ClockEventLogger();
    ~ClockEventLogger();
    
    void logEvent(EventType type, uint64_t masterClockCount, uint32_t audioFrameNumber);
    void clearEvents();
    std::vector<ClockEvent> getEvents() const;
    
    // Analysis
    double calculateAverageFramePeriod() const;
    double calculateClockJitter() const;
    bool detectTimingAnomalies() const;
    
private:
    mutable std::mutex m_eventMutex;
    std::vector<ClockEvent> m_events;
    static constexpr size_t MAX_EVENTS = 10000;
};

} // namespace MS2000
