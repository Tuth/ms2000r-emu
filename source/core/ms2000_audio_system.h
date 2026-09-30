#pragma once

#include "h8s2350_emulator.h"
#include "dsp56362_emulator.h"
#include "ak4522vf_codec.h"
#include "audio_output.h"
#include "ms2000_clock_system.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

namespace MS2000 {

// MS2000 Audio System Configuration
struct MS2000AudioSystemConfig {
    uint32_t sampleRate = 48000;        // MS2000 hardware spec: 48kHz (12.288MHz/256)
    uint32_t bufferSize = 512;
    uint32_t bitDepth = 24;
    bool enableRealTime = true;
    bool enableInput = true;
    bool enableOutput = true;
    float masterVolume = 1.0f;
    std::string audioDevice = "default";
    
    // Clock frequencies as per MS2000 block diagram
    uint32_t systemClock = 12288000;    // 12.288MHz
    uint32_t h8sClock = 10000000;       // 10MHz
    uint32_t dspClock = 80000000;       // 80MHz
};

// Audio Signal Path State
struct AudioSignalPath {
    // Input path (as per block diagram)
    std::vector<float> input1Analog;    // Input1 → InputVR1 → Amp → CODEC IN R
    std::vector<float> input2Analog;    // Input2 → InputVR2 → Amp → CODEC IN L
    
    // Processing path
    std::vector<uint8_t> codecToDSP;    // CODEC SDO → DSP SDI
    std::vector<uint8_t> dspToCodec;    // DSP SDO → CODEC SDI
    
    // Output path
    std::vector<float> outputAnalog;    // CODEC → Amp → Output (Headphones, L/Mono, R)
    
    // Control signals
    bool frameSync;
    bool powerOnMute;
    bool lineMicGainSwitch;
};

// MS2000 Audio System - Complete audio signal path manager
class MS2000AudioSystem {
public:
    MS2000AudioSystem();
    ~MS2000AudioSystem();
    
    // Initialization and Configuration
    bool initialize(const MS2000AudioSystemConfig& config);
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Component Access
    H8S2350Emulator* getH8S() { return m_h8s.get(); }
    DSP56362Emulator* getDSP() { return m_dsp.get(); }
    AK4522VFCodec* getCodec() { return m_codec.get(); }
    AudioOutput* getAudioOutput() { return m_audioOutput.get(); }
    
    // Audio Control
    bool startAudio();
    bool stopAudio();
    bool isAudioRunning() const { return m_audioRunning; }
    
    // Real-time Audio Processing
    void processAudioFrame();
    void updateAudioSignalPath();
    
    // Input/Output Management
    void writeInputSamples(const std::vector<float>& left, const std::vector<float>& right);
    std::vector<std::pair<float, float>> readOutputSamples(uint32_t count);
    
    // Control Interface
    void setMasterVolume(float volume);
    float getMasterVolume() const { return m_masterVolume; }
    
    void setInputGain(uint8_t input, float gain);
    float getInputGain(uint8_t input) const;
    
    void setOutputGain(uint8_t output, float gain);
    float getOutputGain(uint8_t output) const;
    
    void setMute(bool mute);
    bool isMuted() const { return m_mute; }
    
    void setPowerOnMute(bool mute);
    bool isPowerOnMuted() const { return m_powerOnMute; }
    
    void setLineMicGainSwitch(bool line);
    bool isLineMicGainSwitch() const { return m_lineMicGainSwitch; }
    
    // Serial Communication (as per block diagram)
    void writeH8SToDSP(const std::vector<uint8_t>& data);  // H8S TxD → DSP RxD
    std::vector<uint8_t> readDSPToH8S();                   // DSP TxD → H8S RxD
    
    void writeDSPToCodec(const std::vector<uint8_t>& data); // DSP SDO → CODEC SDI
    std::vector<uint8_t> readCodecToDSP();                  // CODEC SDO → DSP SDI
    
    // Clock Management
    void setSystemClock(uint32_t frequency);
    void setH8SClock(uint32_t frequency);
    void setDSPClock(uint32_t frequency);
    void setFrameSync(bool active);
    
    // Status and Monitoring
    bool isInputOverloaded() const;
    bool isOutputClipping() const;
    float getInputLevel(uint8_t input) const;
    float getOutputLevel(uint8_t output) const;
    uint32_t getActiveVoices() const;
    uint32_t getAudioLatency() const;
    
    // Statistics
    uint64_t getProcessedSamples() const { return m_processedSamples; }
    uint64_t getAudioFrames() const { return m_audioFrames; }
    double getCPUUsage() const { return m_cpuUsage; }
    
    // Callbacks
    void setAudioOutputCallback(std::function<void(const std::vector<float>&)> callback);
    void setAudioInputCallback(std::function<void(const std::vector<float>&)> callback);
    void setDSPCommunicationCallback(std::function<void(const std::vector<uint8_t>&)> callback);
    void setH8SCommunicationCallback(std::function<void(const std::vector<uint8_t>&)> callback);
    
    // Error Handling
    std::string getLastError() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }
    
private:
    // Core Components (as per block diagram)
    std::unique_ptr<H8S2350Emulator> m_h8s;
    std::unique_ptr<DSP56362Emulator> m_dsp;
    std::unique_ptr<AK4522VFCodec> m_codec;
    std::unique_ptr<AudioOutput> m_audioOutput;
    std::unique_ptr<MS2000ClockSystem> m_clockSystem;
    std::unique_ptr<ClockDomainManager> m_clockDomainManager;
    std::unique_ptr<ClockEventLogger> m_clockEventLogger;
    
    // Configuration
    MS2000AudioSystemConfig m_config;
    bool m_initialized;
    bool m_audioRunning;
    
    // Audio Signal Path (as per block diagram)
    AudioSignalPath m_signalPath;
    
    // Control Parameters
    float m_masterVolume;
    float m_inputGains[2];  // Input1, Input2
    float m_outputGains[3]; // Headphones, L/Mono, R
    bool m_mute;
    bool m_powerOnMute;
    bool m_lineMicGainSwitch;
    
    // Serial Communication Buffers
    std::queue<std::vector<uint8_t>> m_h8sToDSPBuffer;
    std::queue<std::vector<uint8_t>> m_dspToH8SBuffer;
    std::queue<std::vector<uint8_t>> m_dspToCodecBuffer;
    std::queue<std::vector<uint8_t>> m_codecToDSPBuffer;
    
    // Audio Processing
    std::vector<float> m_inputBuffer;
    std::vector<float> m_outputBuffer;
    std::vector<float> m_tempBuffer;
    
    // Threading
    std::thread m_audioThread;
    std::atomic<bool> m_threadRunning;
    std::mutex m_audioMutex;
    std::mutex m_communicationMutex;
    std::condition_variable m_audioCondition;
    
    // Statistics
    uint64_t m_processedSamples;
    uint64_t m_audioFrames;
    double m_cpuUsage;
    std::chrono::steady_clock::time_point m_lastFrame;
    
    // Callbacks
    std::function<void(const std::vector<float>&)> m_audioOutputCallback;
    std::function<void(const std::vector<float>&)> m_audioInputCallback;
    std::function<void(const std::vector<uint8_t>&)> m_dspCommunicationCallback;
    std::function<void(const std::vector<uint8_t>&)> m_h8sCommunicationCallback;
    
    // Error Handling
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    
    // Internal Methods
    void initializeComponents();
    void initializeAudioSignalPath();
    void initializeSerialCommunication();
    
    void audioThreadFunction();
    void processInputPath();
    void processOutputPath();
    void processDSPCommunication();
    void processH8SCommunication();
    
    void updateInputAmplifiers();
    void updateOutputAmplifiers();
    void updateMasterVolume();
    
    void setError(const std::string& error);
    
    // Audio Conversion Helpers
    std::vector<uint8_t> convertAnalogToDigital(const std::vector<float>& analog);
    std::vector<float> convertDigitalToAnalog(const std::vector<uint8_t>& digital);
    
    // Clock Synchronization
    void synchronizeClocks();
    void generateFrameSync();
    
    // Level Detection
    void detectInputLevels();
    void detectOutputLevels();
};

} // namespace MS2000
