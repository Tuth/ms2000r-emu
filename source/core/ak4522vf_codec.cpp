#include "ak4522vf_codec.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MS2000 {

AK4522VFCodec::AK4522VFCodec()
    : m_initialized(false)
    , m_sampleRate(44100)
    , m_bitDepth(24)
    , m_channels(2)
    , m_inputGain(1.0f)
    , m_outputGain(1.0f)
    , m_mute(false)
    , m_frameSync(false)
    , m_systemClock(12288000)  // 12.288MHz as per MS2000 spec
    , m_64fsClock(2822400)     // 64 * 44.1kHz
    , m_256fsClock(11289600)   // 256 * 44.1kHz
    , m_inputOverload(false)
    , m_outputClipping(false)
    , m_inputLevel(0.0f)
    , m_outputLevel(0.0f)
    , m_sampleCount(0)
{
    // Initialize buffers
    m_inputBuffer.reserve(1024);
    m_outputBuffer.reserve(1024);
    m_analogInputBuffer.reserve(1024);
    m_analogOutputBuffer.reserve(1024);
    m_sdiBuffer.reserve(256);
    m_sdoBuffer.reserve(256);
    
    // Initialize registers
    initializeRegisters();
}

AK4522VFCodec::~AK4522VFCodec() {
    shutdown();
}

bool AK4522VFCodec::initialize(const AK4522VFConfig& config) {
    if (m_initialized) {
        setError("Codec already initialized");
        return false;
    }
    
    m_config = config;
    m_sampleRate = config.sampleRate;
    m_bitDepth = config.bitDepth;
    m_channels = config.channels;
    m_inputGain = config.inputGain;
    m_outputGain = config.outputGain;
    m_mute = config.mute;
    
    // Validate configuration
    if (m_sampleRate != 44100 && m_sampleRate != 48000) {
        setError("Unsupported sample rate: " + std::to_string(m_sampleRate));
        return false;
    }
    
    if (m_bitDepth != 16 && m_bitDepth != 20 && m_bitDepth != 24) {
        setError("Unsupported bit depth: " + std::to_string(m_bitDepth));
        return false;
    }
    
    if (m_channels != 2) {
        setError("Only stereo (2 channels) is supported");
        return false;
    }
    
    // Update clock frequencies based on sample rate
    m_64fsClock = 64 * m_sampleRate;
    m_256fsClock = 256 * m_sampleRate;
    
    // Clear buffers
    m_inputBuffer.clear();
    m_outputBuffer.clear();
    m_analogInputBuffer.clear();
    m_analogOutputBuffer.clear();
    m_sdiBuffer.clear();
    m_sdoBuffer.clear();
    
    m_initialized = true;
    m_sampleCount = 0;
    
    std::cout << "🎵 AK4522VF Codec initialized: " << m_sampleRate << "Hz, " 
              << m_bitDepth << "bit, " << m_channels << "ch" << std::endl;
    
    return true;
}

void AK4522VFCodec::shutdown() {
    if (!m_initialized) return;
    
    m_initialized = false;
    m_frameSync = false;
    
    // Clear all buffers
    m_inputBuffer.clear();
    m_outputBuffer.clear();
    m_analogInputBuffer.clear();
    m_analogOutputBuffer.clear();
    m_sdiBuffer.clear();
    m_sdoBuffer.clear();
    
    std::cout << "🎵 AK4522VF Codec shutdown" << std::endl;
}

void AK4522VFCodec::setSampleRate(uint32_t sampleRate) {
    if (sampleRate != 44100 && sampleRate != 48000) {
        setError("Unsupported sample rate: " + std::to_string(sampleRate));
        return;
    }
    
    m_sampleRate = sampleRate;
    m_64fsClock = 64 * m_sampleRate;
    m_256fsClock = 256 * m_sampleRate;
    
    if (m_registerChangeCallback) {
        m_registerChangeCallback(0x01, m_registers.clock_format);
    }
}

void AK4522VFCodec::setBitDepth(uint32_t bitDepth) {
    if (bitDepth != 16 && bitDepth != 20 && bitDepth != 24) {
        setError("Unsupported bit depth: " + std::to_string(bitDepth));
        return;
    }
    
    m_bitDepth = bitDepth;
    
    if (m_registerChangeCallback) {
        m_registerChangeCallback(0x02, m_registers.interface_format);
    }
}

void AK4522VFCodec::setInputGain(float gain) {
    m_inputGain = std::clamp(gain, 0.0f, 2.0f);
    processInputVolume();
}

void AK4522VFCodec::setOutputGain(float gain) {
    m_outputGain = std::clamp(gain, 0.0f, 2.0f);
    processOutputVolume();
}

void AK4522VFCodec::setMute(bool mute) {
    m_mute = mute;
    
    if (mute) {
        m_registers.power_management |= 0x80;  // Set mute bit
    } else {
        m_registers.power_management &= ~0x80; // Clear mute bit
    }
    
    if (m_registerChangeCallback) {
        m_registerChangeCallback(0x00, m_registers.power_management);
    }
}

uint8_t AK4522VFCodec::readRegister(uint8_t address) {
    if (!isValidRegister(address)) {
        setError("Invalid register address: 0x" + std::to_string(address));
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(m_registerMutex);
    
    switch (address) {
        case 0x00: return m_registers.power_management;
        case 0x01: return m_registers.clock_format;
        case 0x02: return m_registers.interface_format;
        case 0x03: return m_registers.mode_control;
        case 0x04: return m_registers.timer_select;
        case 0x05: return m_registers.lch_input_volume;
        case 0x06: return m_registers.rch_input_volume;
        case 0x07: return m_registers.lch_output_volume;
        case 0x08: return m_registers.rch_output_volume;
        case 0x09: return m_registers.lch_mixer_control;
        case 0x0A: return m_registers.rch_mixer_control;
        default: return 0;
    }
}

void AK4522VFCodec::writeRegister(uint8_t address, uint8_t value) {
    if (!isValidRegister(address)) {
        setError("Invalid register address: 0x" + std::to_string(address));
        return;
    }
    
    validateRegisterValue(address, value);
    
    std::lock_guard<std::mutex> lock(m_registerMutex);
    
    switch (address) {
        case 0x00: 
            m_registers.power_management = value;
            m_mute = (value & 0x80) != 0;
            break;
        case 0x01: 
            m_registers.clock_format = value;
            break;
        case 0x02: 
            m_registers.interface_format = value;
            break;
        case 0x03: 
            m_registers.mode_control = value;
            break;
        case 0x04: 
            m_registers.timer_select = value;
            break;
        case 0x05: 
            m_registers.lch_input_volume = value;
            break;
        case 0x06: 
            m_registers.rch_input_volume = value;
            break;
        case 0x07: 
            m_registers.lch_output_volume = value;
            break;
        case 0x08: 
            m_registers.rch_output_volume = value;
            break;
        case 0x09: 
            m_registers.lch_mixer_control = value;
            break;
        case 0x0A: 
            m_registers.rch_mixer_control = value;
            break;
    }
    
    if (m_registerChangeCallback) {
        m_registerChangeCallback(address, value);
    }
}

void AK4522VFCodec::writeSDI(const std::vector<uint8_t>& data) {
    if (!m_initialized) return;
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Convert digital data to analog output
    convertDigitalToAnalog(data);
    
    // Store in SDI buffer for processing
    m_sdiBuffer.insert(m_sdiBuffer.end(), data.begin(), data.end());
}

std::vector<uint8_t> AK4522VFCodec::readSDO() {
    if (!m_initialized) return {};
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Convert analog input to digital data
    std::vector<uint8_t> result = m_sdoBuffer;
    m_sdoBuffer.clear();
    
    return result;
}

void AK4522VFCodec::setFrameSync(bool active) {
    m_frameSync = active;
    
    if (active) {
        processAudioFrame();
    }
}

void AK4522VFCodec::setSystemClock(uint32_t frequency) {
    m_systemClock = frequency;
}

void AK4522VFCodec::set64fsClock(uint32_t frequency) {
    m_64fsClock = frequency;
}

void AK4522VFCodec::set256fsClock(uint32_t frequency) {
    m_256fsClock = frequency;
}

void AK4522VFCodec::writeInputSamples(const std::vector<AudioSample>& samples) {
    if (!m_initialized) return;
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Convert to analog format
    for (const auto& sample : samples) {
        m_analogInputBuffer.push_back(sample.left);
        m_analogInputBuffer.push_back(sample.right);
    }
    
    // Convert analog to digital for SDO
    convertAnalogToDigital(m_analogInputBuffer);
    
    if (m_inputCallback) {
        m_inputCallback(samples);
    }
}

std::vector<AudioSample> AK4522VFCodec::readOutputSamples(uint32_t count) {
    if (!m_initialized) return {};
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    std::vector<AudioSample> result;
    result.reserve(count);
    
    uint32_t available = std::min(count, static_cast<uint32_t>(m_outputBuffer.size()));
    
    for (uint32_t i = 0; i < available; ++i) {
        result.push_back(m_outputBuffer[i]);
    }
    
    // Remove processed samples
    m_outputBuffer.erase(m_outputBuffer.begin(), m_outputBuffer.begin() + available);
    
    return result;
}

void AK4522VFCodec::processAudioFrame() {
    if (!m_initialized || m_mute) return;
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Process input volume
    processInputVolume();
    
    // Apply filters
    applyFilters();
    
    // Process output volume
    processOutputVolume();
    
    // Detect overload conditions
    detectOverload();
    
    m_sampleCount++;
}

void AK4522VFCodec::convertAnalogToDigital(const std::vector<float>& analogInput) {
    if (!m_initialized) return;
    
    std::vector<uint8_t> digitalData;
    digitalData.reserve(analogInput.size() * (m_bitDepth / 8));
    
    for (float sample : analogInput) {
        uint32_t digitalValue = convertAnalogToDigital(sample);
        
        // Convert to bytes based on bit depth
        if (m_bitDepth == 16) {
            digitalData.push_back((digitalValue >> 8) & 0xFF);
            digitalData.push_back(digitalValue & 0xFF);
        } else if (m_bitDepth == 20) {
            digitalData.push_back((digitalValue >> 12) & 0xFF);
            digitalData.push_back((digitalValue >> 4) & 0xFF);
            digitalData.push_back((digitalValue << 4) & 0xF0);
        } else if (m_bitDepth == 24) {
            digitalData.push_back((digitalValue >> 16) & 0xFF);
            digitalData.push_back((digitalValue >> 8) & 0xFF);
            digitalData.push_back(digitalValue & 0xFF);
        }
    }
    
    m_sdoBuffer.insert(m_sdoBuffer.end(), digitalData.begin(), digitalData.end());
}

void AK4522VFCodec::convertDigitalToAnalog(const std::vector<uint8_t>& digitalInput) {
    if (!m_initialized) return;
    
    std::vector<AudioSample> outputSamples;
    size_t bytesPerSample = m_bitDepth / 8;
    
    for (size_t i = 0; i < digitalInput.size(); i += bytesPerSample * 2) { // *2 for stereo
        if (i + bytesPerSample * 2 > digitalInput.size()) break;
        
        // Convert left channel
        uint32_t leftDigital = 0;
        if (m_bitDepth == 16) {
            leftDigital = (digitalInput[i] << 8) | digitalInput[i + 1];
        } else if (m_bitDepth == 20) {
            leftDigital = (digitalInput[i] << 12) | (digitalInput[i + 1] << 4) | (digitalInput[i + 2] >> 4);
        } else if (m_bitDepth == 24) {
            leftDigital = (digitalInput[i] << 16) | (digitalInput[i + 1] << 8) | digitalInput[i + 2];
        }
        
        // Convert right channel
        uint32_t rightDigital = 0;
        if (m_bitDepth == 16) {
            rightDigital = (digitalInput[i + 2] << 8) | digitalInput[i + 3];
        } else if (m_bitDepth == 20) {
            rightDigital = (digitalInput[i + 3] << 12) | (digitalInput[i + 4] << 4) | (digitalInput[i + 5] >> 4);
        } else if (m_bitDepth == 24) {
            rightDigital = (digitalInput[i + 3] << 16) | (digitalInput[i + 4] << 8) | digitalInput[i + 5];
        }
        
        float leftAnalog = convertDigitalToAnalog(leftDigital);
        float rightAnalog = convertDigitalToAnalog(rightDigital);
        
        outputSamples.emplace_back(leftAnalog, rightAnalog, m_sampleCount);
    }
    
    m_outputBuffer.insert(m_outputBuffer.end(), outputSamples.begin(), outputSamples.end());
    
    if (m_outputCallback) {
        m_outputCallback(outputSamples);
    }
}

void AK4522VFCodec::setInputCallback(std::function<void(const std::vector<AudioSample>&)> callback) {
    m_inputCallback = callback;
}

void AK4522VFCodec::setOutputCallback(std::function<void(const std::vector<AudioSample>&)> callback) {
    m_outputCallback = callback;
}

void AK4522VFCodec::setRegisterChangeCallback(std::function<void(uint8_t, uint8_t)> callback) {
    m_registerChangeCallback = callback;
}

void AK4522VFCodec::initializeRegisters() {
    // Power Management: Normal operation, unmuted
    m_registers.power_management = 0x00;
    
    // Clock Format: Master mode, 44.1kHz
    m_registers.clock_format = 0x00;
    
    // Interface Format: I2S, 24-bit
    m_registers.interface_format = 0x02;
    
    // Mode Control: Normal operation
    m_registers.mode_control = 0x00;
    
    // Timer Select: Default
    m_registers.timer_select = 0x00;
    
    // Input Volume: 0dB (maximum)
    m_registers.lch_input_volume = 0xFF;
    m_registers.rch_input_volume = 0xFF;
    
    // Output Volume: 0dB (maximum)
    m_registers.lch_output_volume = 0xFF;
    m_registers.rch_output_volume = 0xFF;
    
    // Mixer Control: Normal routing
    m_registers.lch_mixer_control = 0x00;
    m_registers.rch_mixer_control = 0x00;
    
    // Reserved registers
    std::fill(std::begin(m_registers.reserved), std::end(m_registers.reserved), 0x00);
}

void AK4522VFCodec::updateAudioProcessing() {
    // Update processing based on register changes
    processInputVolume();
    processOutputVolume();
}

void AK4522VFCodec::processInputVolume() {
    // Apply input volume from registers
    float leftGain = (m_registers.lch_input_volume & 0xFF) / 255.0f;
    float rightGain = (m_registers.rch_input_volume & 0xFF) / 255.0f;
    
    for (auto& sample : m_inputBuffer) {
        sample.left *= leftGain * m_inputGain;
        sample.right *= rightGain * m_inputGain;
    }
}

void AK4522VFCodec::processOutputVolume() {
    // Apply output volume from registers
    float leftGain = (m_registers.lch_output_volume & 0xFF) / 255.0f;
    float rightGain = (m_registers.rch_output_volume & 0xFF) / 255.0f;
    
    for (auto& sample : m_outputBuffer) {
        sample.left *= leftGain * m_outputGain;
        sample.right *= rightGain * m_outputGain;
    }
}

void AK4522VFCodec::detectOverload() {
    // Check input overload
    m_inputOverload = false;
    m_inputLevel = 0.0f;
    
    for (const auto& sample : m_inputBuffer) {
        float level = std::max(std::abs(sample.left), std::abs(sample.right));
        m_inputLevel = std::max(m_inputLevel, level);
        
        if (level > 0.95f) { // 95% of full scale
            m_inputOverload = true;
        }
    }
    
    // Check output clipping
    m_outputClipping = false;
    m_outputLevel = 0.0f;
    
    for (const auto& sample : m_outputBuffer) {
        float level = std::max(std::abs(sample.left), std::abs(sample.right));
        m_outputLevel = std::max(m_outputLevel, level);
        
        if (level > 1.0f) { // Clipping
            m_outputClipping = true;
        }
    }
}

void AK4522VFCodec::applyFilters() {
    // Simple low-pass filter to simulate analog characteristics
    static float leftHistory[2] = {0.0f, 0.0f};
    static float rightHistory[2] = {0.0f, 0.0f};
    
    for (auto& sample : m_outputBuffer) {
        // Simple first-order low-pass filter
        float alpha = 0.1f;
        sample.left = alpha * sample.left + (1.0f - alpha) * leftHistory[0];
        sample.right = alpha * sample.right + (1.0f - alpha) * rightHistory[0];
        
        leftHistory[1] = leftHistory[0];
        leftHistory[0] = sample.left;
        rightHistory[1] = rightHistory[0];
        rightHistory[0] = sample.right;
    }
}

float AK4522VFCodec::convertDigitalToAnalog(uint32_t digitalValue) {
    // Convert digital value to analog (-1.0 to 1.0)
    uint32_t maxValue = (1 << m_bitDepth) - 1;
    uint32_t midPoint = maxValue / 2;
    
    if (digitalValue > maxValue) digitalValue = maxValue;
    
    float analog = (static_cast<float>(digitalValue) - midPoint) / midPoint;
    return std::clamp(analog, -1.0f, 1.0f);
}

uint32_t AK4522VFCodec::convertAnalogToDigital(float analogValue) {
    // Convert analog value (-1.0 to 1.0) to digital
    analogValue = std::clamp(analogValue, -1.0f, 1.0f);
    
    uint32_t maxValue = (1 << m_bitDepth) - 1;
    uint32_t midPoint = maxValue / 2;
    
    uint32_t digital = static_cast<uint32_t>((analogValue + 1.0f) * midPoint);
    return std::clamp(digital, 0u, maxValue);
}

bool AK4522VFCodec::isValidRegister(uint8_t address) const {
    return address <= 0x0A; // Valid registers are 0x00-0x0A
}

void AK4522VFCodec::validateRegisterValue(uint8_t address, uint8_t value) {
    // Basic validation for register values
    switch (address) {
        case 0x00: // Power Management
            // Bits 7-6 are reserved, should be 0
            if ((value & 0xC0) != 0) {
                setError("Invalid power management register value");
            }
            break;
        case 0x01: // Clock Format
            // Bits 7-4 are reserved, should be 0
            if ((value & 0xF0) != 0) {
                setError("Invalid clock format register value");
            }
            break;
        case 0x02: // Interface Format
            // Bits 7-4 are reserved, should be 0
            if ((value & 0xF0) != 0) {
                setError("Invalid interface format register value");
            }
            break;
    }
}

void AK4522VFCodec::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastError = error;
    std::cerr << "🎵 AK4522VF Error: " << error << std::endl;
}

} // namespace MS2000
