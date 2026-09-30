#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace MS2000 {

// AK4522VF Audio Codec Configuration
struct AK4522VFConfig {
    uint32_t sampleRate = 44100;
    uint32_t bitDepth = 24;
    uint32_t channels = 2;  // Stereo
    bool enableInput = true;
    bool enableOutput = true;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    bool mute = false;
};

// AK4522VF Register Map
struct AK4522VFRegisters {
    uint8_t power_management;      // 0x00
    uint8_t clock_format;          // 0x01
    uint8_t interface_format;      // 0x02
    uint8_t mode_control;          // 0x03
    uint8_t timer_select;          // 0x04
    uint8_t lch_input_volume;      // 0x05
    uint8_t rch_input_volume;      // 0x06
    uint8_t lch_output_volume;     // 0x07
    uint8_t rch_output_volume;     // 0x08
    uint8_t lch_mixer_control;     // 0x09
    uint8_t rch_mixer_control;     // 0x0A
    uint8_t reserved[5];           // 0x0B-0x0F
};

// Audio Sample Buffer
struct AudioSample {
    float left;
    float right;
    uint64_t timestamp;
    
    AudioSample(float l = 0.0f, float r = 0.0f, uint64_t ts = 0)
        : left(l), right(r), timestamp(ts) {}
};

// AK4522VF Audio Codec Emulator
class AK4522VFCodec {
public:
    AK4522VFCodec();
    ~AK4522VFCodec();
    
    // Initialization and Configuration
    bool initialize(const AK4522VFConfig& config);
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Configuration
    void setSampleRate(uint32_t sampleRate);
    uint32_t getSampleRate() const { return m_sampleRate; }
    
    void setBitDepth(uint32_t bitDepth);
    uint32_t getBitDepth() const { return m_bitDepth; }
    
    void setInputGain(float gain);
    float getInputGain() const { return m_inputGain; }
    
    void setOutputGain(float gain);
    float getOutputGain() const { return m_outputGain; }
    
    void setMute(bool mute);
    bool isMuted() const { return m_mute; }
    
    // Register Access (SPI Interface)
    uint8_t readRegister(uint8_t address);
    void writeRegister(uint8_t address, uint8_t value);
    
    // Serial Audio Interface (SDI/SDO)
    void writeSDI(const std::vector<uint8_t>& data);
    std::vector<uint8_t> readSDO();
    
    // Frame Sync and Clock
    void setFrameSync(bool active);
    void setSystemClock(uint32_t frequency);
    void set64fsClock(uint32_t frequency);
    void set256fsClock(uint32_t frequency);
    
    // Audio I/O
    void writeInputSamples(const std::vector<AudioSample>& samples);
    std::vector<AudioSample> readOutputSamples(uint32_t count);
    
    // Real-time Audio Processing
    void processAudioFrame();
    void convertAnalogToDigital(const std::vector<float>& analogInput);
    void convertDigitalToAnalog(const std::vector<uint8_t>& digitalInput);
    
    // Status and Control
    bool isInputOverloaded() const { return m_inputOverload; }
    bool isOutputClipping() const { return m_outputClipping; }
    float getInputLevel() const { return m_inputLevel; }
    float getOutputLevel() const { return m_outputLevel; }
    
    // Callbacks
    void setInputCallback(std::function<void(const std::vector<AudioSample>&)> callback);
    void setOutputCallback(std::function<void(const std::vector<AudioSample>&)> callback);
    void setRegisterChangeCallback(std::function<void(uint8_t, uint8_t)> callback);
    
    // Error Handling
    std::string getLastError() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }
    
private:
    // Internal Configuration
    AK4522VFConfig m_config;
    bool m_initialized;
    
    // Audio Parameters
    uint32_t m_sampleRate;
    uint32_t m_bitDepth;
    uint32_t m_channels;
    float m_inputGain;
    float m_outputGain;
    bool m_mute;
    
    // Registers
    AK4522VFRegisters m_registers;
    
    // Serial Interface
    std::vector<uint8_t> m_sdiBuffer;
    std::vector<uint8_t> m_sdoBuffer;
    bool m_frameSync;
    uint32_t m_systemClock;
    uint32_t m_64fsClock;
    uint32_t m_256fsClock;
    
    // Audio Buffers
    std::vector<AudioSample> m_inputBuffer;
    std::vector<AudioSample> m_outputBuffer;
    std::vector<float> m_analogInputBuffer;
    std::vector<float> m_analogOutputBuffer;
    
    // Processing State
    bool m_inputOverload;
    bool m_outputClipping;
    float m_inputLevel;
    float m_outputLevel;
    uint64_t m_sampleCount;
    
    // Callbacks
    std::function<void(const std::vector<AudioSample>&)> m_inputCallback;
    std::function<void(const std::vector<AudioSample>&)> m_outputCallback;
    std::function<void(uint8_t, uint8_t)> m_registerChangeCallback;
    
    // Threading
    std::mutex m_audioMutex;
    std::mutex m_registerMutex;
    
    // Error Handling
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    
    // Internal Methods
    void initializeRegisters();
    void updateAudioProcessing();
    void processInputVolume();
    void processOutputVolume();
    void detectOverload();
    void applyFilters();
    void setError(const std::string& error);
    
    // Audio Conversion
    float convertDigitalToAnalog(uint32_t digitalValue);
    uint32_t convertAnalogToDigital(float analogValue);
    
    // Register Validation
    bool isValidRegister(uint8_t address) const;
    void validateRegisterValue(uint8_t address, uint8_t value);
};

} // namespace MS2000
