#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>

namespace MS2000 {

// Audio Output Configuration
struct AudioConfig {
    uint32_t sampleRate = 44100;
    uint32_t bufferSize = 512;
    uint32_t channels = 2;  // Stereo
    uint32_t bitsPerSample = 16;
    float masterVolume = 1.0f;
    bool enableRealTime = true;
    std::string deviceName = "default";
};

// Audio Sample Buffer
struct AudioBuffer {
    std::vector<float> samples;
    uint32_t sampleRate;
    uint32_t channels;
    uint64_t timestamp;
    
    AudioBuffer(uint32_t size = 0, uint32_t rate = 44100, uint32_t ch = 2)
        : samples(size), sampleRate(rate), channels(ch), timestamp(0) {}
};

// Audio Device Information
struct AudioDeviceInfo {
    std::string name;
    std::string id;
    uint32_t maxSampleRate;
    uint32_t maxChannels;
    bool isInput;
    bool isOutput;
    bool isDefault;
};

// Real-time Audio Output Interface
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    
    // Initialization and Configuration
    bool initialize(const AudioConfig& config);
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Device Management
    std::vector<AudioDeviceInfo> getAvailableDevices();
    bool setDevice(const std::string& deviceId);
    std::string getCurrentDevice() const { return m_currentDevice; }
    
    // Audio Control
    bool start();
    bool stop();
    bool isPlaying() const { return m_playing; }
    
    void setMasterVolume(float volume);
    float getMasterVolume() const { return m_masterVolume; }
    
    void setSampleRate(uint32_t sampleRate);
    uint32_t getSampleRate() const { return m_sampleRate; }
    
    void setBufferSize(uint32_t bufferSize);
    uint32_t getBufferSize() const { return m_bufferSize; }
    
    // Audio Data Interface
    void setAudioCallback(std::function<void(AudioBuffer&)> callback);
    void pushAudioData(const std::vector<float>& samples);
    void pushAudioData(const float* samples, uint32_t count);
    
    // Statistics and Monitoring
    uint32_t getLatency() const { return m_latency; }
    uint32_t getUnderruns() const { return m_underruns; }
    uint32_t getOverruns() const { return m_overruns; }
    float getCPUUsage() const { return m_cpuUsage; }
    
    // Error Handling
    std::string getLastError() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }
    
private:
    // Core Audio System
    bool initializeAudioSystem();
    void shutdownAudioSystem();
    void audioThreadFunction();
    void processAudioBuffer();
    
    // Platform-specific audio implementation
    bool initializePlatformAudio();
    void shutdownPlatformAudio();
    bool startPlatformAudio();
    bool stopPlatformAudio();
    uint32_t writePlatformAudio(const float* data, uint32_t frames);
    
    // Buffer Management
    void resizeBuffers();
    void clearBuffers();
    bool hasEnoughData() const;
    
    // Error Handling
    void setError(const std::string& error);
    
    // Configuration
    AudioConfig m_config;
    std::string m_currentDevice;
    bool m_initialized;
    bool m_playing;
    
    // Audio Processing
    std::function<void(AudioBuffer&)> m_audioCallback;
    std::vector<float> m_inputBuffer;
    std::vector<float> m_outputBuffer;
    std::queue<AudioBuffer> m_audioQueue;
    
    // Threading
    std::thread m_audioThread;
    std::atomic<bool> m_threadRunning;
    std::mutex m_bufferMutex;
    std::condition_variable m_bufferCondition;
    
    // Statistics
    uint32_t m_latency;
    uint32_t m_underruns;
    uint32_t m_overruns;
    float m_cpuUsage;
    float m_masterVolume;
    
    // Platform-specific data
    struct PlatformData;
    std::unique_ptr<PlatformData> m_platformData;
    
    // Error handling
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    
    // Audio parameters
    uint32_t m_sampleRate;
    uint32_t m_bufferSize;
    uint32_t m_channels;
    uint32_t m_bitsPerSample;
};

// MS2000-Specific Audio Interface
class MS2000AudioInterface {
public:
    MS2000AudioInterface();
    ~MS2000AudioInterface();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Audio Generation
    void generateAudioFrame();
    void processDSPAudio();
    void mixAudioChannels();
    
    // Control Interface
    void setOscillatorFrequency(uint8_t osc, float frequency);
    void setOscillatorWaveform(uint8_t osc, uint8_t waveform);
    void setOscillatorAmplitude(uint8_t osc, float amplitude);
    void enableOscillator(uint8_t osc, bool enabled);
    
    void setFilterCutoff(uint8_t filter, float cutoff);
    void setFilterResonance(uint8_t filter, float resonance);
    void setFilterType(uint8_t filter, uint8_t type);
    void enableFilter(uint8_t filter, bool enabled);
    
    void setEnvelopeADSR(uint8_t env, float attack, float decay, float sustain, float release);
    void triggerEnvelope(uint8_t env);
    void releaseEnvelope(uint8_t env);
    
    void setEffectType(uint8_t effect, uint8_t type);
    void setEffectParameter(uint8_t effect, uint8_t param, float value);
    void enableEffect(uint8_t effect, bool enabled);
    
    // Master Controls
    void setMasterVolume(float volume);
    void setMasterTune(float tune);
    void setMasterPan(float pan);
    
    // Real-time Controls
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void allNotesOff();
    void setPitchBend(float bend);
    void setModulation(float mod);
    
    // Audio Output
    void startAudio();
    void stopAudio();
    bool isAudioRunning() const;
    
    // Statistics
    uint32_t getAudioLatency() const;
    float getAudioLevel() const;
    uint32_t getActiveVoices() const;
    
    // Callback Interface
    void setAudioOutputCallback(std::function<void(const std::vector<float>&)> callback);
    void setMIDIInputCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback);
    void setMIDIOutputCallback(std::function<void(uint8_t, uint8_t, uint8_t)> callback);
    
private:
    // Audio Processing
    void updateOscillators();
    void updateFilters();
    void updateEnvelopes();
    void updateEffects();
    void applyMasterControls();
    
    // Voice Management
    struct Voice {
        uint8_t note;
        uint8_t velocity;
        float frequency;
        float amplitude;
        float phase[4];  // For each oscillator
        float envelope_level;
        uint8_t envelope_stage;
        bool active;
        uint64_t start_time;
    };
    
    std::vector<Voice> m_voices;
    uint8_t m_max_voices;
    
    // Audio Engine
    std::unique_ptr<AudioOutput> m_audioOutput;
    std::vector<float> m_audioBuffer;
    std::vector<float> m_tempBuffer;
    
    // DSP Integration
    std::function<void(const std::vector<float>&)> m_audioCallback;
    std::function<void(uint8_t, uint8_t, uint8_t)> m_midiInputCallback;
    std::function<void(uint8_t, uint8_t, uint8_t)> m_midiOutputCallback;
    
    // Control Parameters
    float m_masterVolume;
    float m_masterTune;
    float m_masterPan;
    float m_pitchBend;
    float m_modulation;
    
    // Statistics
    uint32_t m_activeVoices;
    float m_audioLevel;
    uint64_t m_frameCount;
    
    // Threading
    std::thread m_audioThread;
    std::atomic<bool> m_threadRunning;
    std::mutex m_audioMutex;
    
    // Timing
    std::chrono::steady_clock::time_point m_lastFrame;
    uint32_t m_frameRate;
};

} // namespace MS2000
