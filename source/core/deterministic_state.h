// fw15.txt: PRNG seed control and deterministic state management
#pragma once
#include <random>
#include <cstdint>
#include <chrono>
#include <string>

class DeterministicState {
public:
    struct Config {
        uint64_t prngSeed = 0;          // 0 = use current time
        bool fixedSeed = false;         // true = use prngSeed, false = auto-generate
        bool deterministicTiming = true; // use fixed 1ms ticks instead of real-time
        uint32_t tickDurationMs = 1;    // deterministic tick duration
    };

    DeterministicState() : m_config{}, m_initialized(false) {
        resetWithConfig(m_config);
    }

    void resetWithConfig(const Config& config) {
        m_config = config;
        
        // Initialize PRNG seed
        if (m_config.fixedSeed) {
            m_currentSeed = m_config.prngSeed;
        } else {
            // Auto-generate seed from high-resolution clock
            auto now = std::chrono::high_resolution_clock::now();
            m_currentSeed = static_cast<uint64_t>(now.time_since_epoch().count());
        }
        
        // Initialize RNG with determined seed
        m_rng.seed(static_cast<std::mt19937_64::result_type>(m_currentSeed));
        
        // Reset timing
        m_startTime = std::chrono::steady_clock::now();
        m_tickCount = 0;
        m_initialized = true;
    }

    // PRNG access
    std::mt19937_64& getRNG() { return m_rng; }
    uint64_t getCurrentSeed() const { return m_currentSeed; }
    
    // Generate random numbers (for consistent API)
    uint32_t randomUint32() { return static_cast<uint32_t>(m_rng()); }
    uint64_t randomUint64() { return m_rng(); }
    double randomDouble() { return m_doubleDist(m_rng); }
    
    // Timing control
    bool isDeterministicTiming() const { return m_config.deterministicTiming; }
    uint32_t getTickDurationMs() const { return m_config.tickDurationMs; }
    
    // Advance deterministic time (call every tick)
    void advanceTick() {
        m_tickCount++;
    }
    
    uint64_t getTickCount() const { return m_tickCount; }
    
    // Get elapsed time (real or simulated)
    uint64_t getElapsedMs() const {
        if (m_config.deterministicTiming) {
            return m_tickCount * m_config.tickDurationMs;
        } else {
            auto elapsed = std::chrono::steady_clock::now() - m_startTime;
            return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        }
    }
    
    // State serialization for .mpcap files
    struct SerializedState {
        uint64_t prngSeed;
        uint64_t tickCount;
        uint64_t elapsedMs;
        Config config;
    };
    
    SerializedState serialize() const {
        return SerializedState{
            m_currentSeed,
            m_tickCount,
            getElapsedMs(),
            m_config
        };
    }
    
    void deserialize(const SerializedState& state) {
        m_config = state.config;
        m_currentSeed = state.prngSeed;
        m_tickCount = state.tickCount;
        m_rng.seed(static_cast<std::mt19937_64::result_type>(m_currentSeed));
        
        // Skip RNG state to match the recorded position
        // This is approximate - for perfect replay, we'd need full RNG state
        for (uint64_t i = 0; i < m_tickCount; i++) {
            (void)m_rng(); // Advance RNG state, suppress nodiscard warning
        }
        
        m_startTime = std::chrono::steady_clock::now() - 
            std::chrono::milliseconds(state.elapsedMs);
        m_initialized = true;
    }
    
    const Config& getConfig() const { return m_config; }
    bool isInitialized() const { return m_initialized; }
    
private:
    Config m_config;
    std::mt19937_64 m_rng;
    std::uniform_real_distribution<double> m_doubleDist{0.0, 1.0};
    uint64_t m_currentSeed;
    uint64_t m_tickCount;
    std::chrono::steady_clock::time_point m_startTime;
    bool m_initialized;
};