#include "ms2000_audio_system.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace MS2000 {

MS2000AudioSystem::MS2000AudioSystem()
    : m_initialized(false)
    , m_audioRunning(false)
    , m_masterVolume(1.0f)
    , m_mute(false)
    , m_powerOnMute(false)
    , m_lineMicGainSwitch(false)
    , m_processedSamples(0)
    , m_audioFrames(0)
    , m_cpuUsage(0.0)
    , m_lastFrame(std::chrono::high_resolution_clock::now())
{
    // Initialize gain arrays
    std::fill(std::begin(m_inputGains), std::end(m_inputGains), 1.0f);
    std::fill(std::begin(m_outputGains), std::end(m_outputGains), 1.0f);
    
    // Initialize signal path
    m_signalPath.frameSync = false;
    m_signalPath.powerOnMute = false;
    m_signalPath.lineMicGainSwitch = false;
    
    // Reserve buffers
    m_inputBuffer.reserve(2048);
    m_outputBuffer.reserve(2048);
    m_tempBuffer.reserve(2048);
}

MS2000AudioSystem::~MS2000AudioSystem() {
    shutdown();
}

bool MS2000AudioSystem::initialize(const MS2000AudioSystemConfig& config) {
    if (m_initialized) {
        setError("Audio system already initialized");
        return false;
    }
    
    m_config = config;
    
    try {
        // Initialize components
        initializeComponents();
        
        // Initialize audio signal path
        initializeAudioSignalPath();
        
        // Initialize serial communication
        initializeSerialCommunication();
        
        m_initialized = true;
        
        std::cout << "🎵 MS2000 Audio System initialized:" << std::endl;
        std::cout << "   Sample Rate: " << m_config.sampleRate << "Hz" << std::endl;
        std::cout << "   Buffer Size: " << m_config.bufferSize << " samples" << std::endl;
        std::cout << "   Bit Depth: " << m_config.bitDepth << " bits" << std::endl;
        std::cout << "   System Clock: " << m_config.systemClock / 1000000 << "MHz" << std::endl;
        std::cout << "   H8S Clock: " << m_config.h8sClock / 1000000 << "MHz" << std::endl;
        std::cout << "   DSP Clock: " << m_config.dspClock / 1000000 << "MHz" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        setError("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void MS2000AudioSystem::shutdown() {
    if (!m_initialized) return;
    
    // Stop audio processing
    stopAudio();
    
    // Shutdown components
    if (m_audioOutput) m_audioOutput->shutdown();
    if (m_codec) m_codec->shutdown();
    if (m_dsp) m_dsp->shutdown();
    // Note: H8S2350Emulator doesn't have a shutdown method, it's handled by destructor
    
    m_initialized = false;
    
    std::cout << "🎵 MS2000 Audio System shutdown" << std::endl;
}

bool MS2000AudioSystem::startAudio() {
    if (!m_initialized) {
        setError("Audio system not initialized");
        return false;
    }
    
    if (m_audioRunning) {
        setError("Audio already running");
        return false;
    }
    
    try {
        // Start audio output
        if (!m_audioOutput->start()) {
            setError("Failed to start audio output");
            return false;
        }
        
        // Start audio processing thread
        m_threadRunning = true;
        m_audioThread = std::thread(&MS2000AudioSystem::audioThreadFunction, this);
        
        m_audioRunning = true;
        m_lastFrame = std::chrono::high_resolution_clock::now();
        
        std::cout << "🎵 MS2000 Audio System started" << std::endl;
        return true;
    } catch (const std::exception& e) {
        setError("Failed to start audio: " + std::string(e.what()));
        return false;
    }
}

bool MS2000AudioSystem::stopAudio() {
    if (!m_audioRunning) return true;
    
    try {
        // Stop audio processing thread
        m_threadRunning = false;
        m_audioCondition.notify_all();
        
        if (m_audioThread.joinable()) {
            m_audioThread.join();
        }
        
        // Stop audio output
        if (m_audioOutput) {
            m_audioOutput->stop();
        }
        
        m_audioRunning = false;
        
        std::cout << "🎵 MS2000 Audio System stopped" << std::endl;
        return true;
    } catch (const std::exception& e) {
        setError("Failed to stop audio: " + std::string(e.what()));
        return false;
    }
}

void MS2000AudioSystem::processAudioFrame() {
    if (!m_initialized || !m_audioRunning) return;
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Process input path (as per block diagram)
    processInputPath();
    
    // Process DSP communication
    processDSPCommunication();
    
    // Process H8S communication
    processH8SCommunication();
    
    // Process output path
    processOutputPath();
    
    // Update statistics
    m_audioFrames++;
    auto now = std::chrono::high_resolution_clock::now();
    auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrame);
    m_lastFrame = now;
    
    // Calculate CPU usage
    double expectedFrameTime = 1000000.0 / m_config.sampleRate * m_config.bufferSize;
    m_cpuUsage = (frameTime.count() / expectedFrameTime) * 100.0;
}

void MS2000AudioSystem::updateAudioSignalPath() {
    if (!m_initialized) return;
    
    // Update input amplifiers
    updateInputAmplifiers();
    
    // Update output amplifiers
    updateOutputAmplifiers();
    
    // Update master volume
    updateMasterVolume();
    
    // Synchronize clocks
    synchronizeClocks();
    
    // Generate frame sync
    generateFrameSync();
    
    // Detect levels
    detectInputLevels();
    detectOutputLevels();
}

void MS2000AudioSystem::writeInputSamples(const std::vector<float>& left, const std::vector<float>& right) {
    if (!m_initialized) return;
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Apply input gains and store in signal path
    m_signalPath.input1Analog.clear();
    m_signalPath.input2Analog.clear();
    
    size_t size = std::min(left.size(), right.size());
    m_signalPath.input1Analog.reserve(size);
    m_signalPath.input2Analog.reserve(size);
    
    for (size_t i = 0; i < size; ++i) {
        m_signalPath.input1Analog.push_back(left[i] * m_inputGains[0]);
        m_signalPath.input2Analog.push_back(right[i] * m_inputGains[1]);
    }
    
    // Convert to digital for codec
    auto digitalData = convertAnalogToDigital(m_signalPath.input1Analog);
    m_codec->writeSDI(digitalData);
    
    digitalData = convertAnalogToDigital(m_signalPath.input2Analog);
    m_codec->writeSDI(digitalData);
    
    m_processedSamples += size;
}

std::vector<std::pair<float, float>> MS2000AudioSystem::readOutputSamples(uint32_t count) {
    if (!m_initialized) return {};
    
    std::lock_guard<std::mutex> lock(m_audioMutex);
    
    // Read from codec
    auto samples = m_codec->readOutputSamples(count);
    
    std::vector<std::pair<float, float>> result;
    result.reserve(samples.size());
    
    for (const auto& sample : samples) {
        // Apply output gains and master volume
        float left = sample.left * m_outputGains[1] * m_masterVolume;  // L/Mono
        float right = sample.right * m_outputGains[2] * m_masterVolume; // R
        
        // Apply power-on mute
        if (m_powerOnMute) {
            left = 0.0f;
            right = 0.0f;
        }
        
        result.emplace_back(left, right);
    }
    
    return result;
}

void MS2000AudioSystem::setMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 2.0f);
    updateMasterVolume();
}

void MS2000AudioSystem::setInputGain(uint8_t input, float gain) {
    if (input < 2) {
        m_inputGains[input] = std::clamp(gain, 0.0f, 2.0f);
        updateInputAmplifiers();
    }
}

float MS2000AudioSystem::getInputGain(uint8_t input) const {
    if (input < 2) {
        return m_inputGains[input];
    }
    return 0.0f;
}

void MS2000AudioSystem::setOutputGain(uint8_t output, float gain) {
    if (output < 3) {
        m_outputGains[output] = std::clamp(gain, 0.0f, 2.0f);
        updateOutputAmplifiers();
    }
}

float MS2000AudioSystem::getOutputGain(uint8_t output) const {
    if (output < 3) {
        return m_outputGains[output];
    }
    return 0.0f;
}

void MS2000AudioSystem::setMute(bool mute) {
    m_mute = mute;
    if (m_codec) {
        m_codec->setMute(mute);
    }
}

void MS2000AudioSystem::setPowerOnMute(bool mute) {
    m_powerOnMute = mute;
    m_signalPath.powerOnMute = mute;
}

void MS2000AudioSystem::setLineMicGainSwitch(bool line) {
    m_lineMicGainSwitch = line;
    m_signalPath.lineMicGainSwitch = line;
    
    // Adjust input gain based on line/mic switch
    float gainMultiplier = line ? 1.0f : 10.0f; // Mic has higher gain
    m_inputGains[1] *= gainMultiplier; // Input2 (mic input)
}

void MS2000AudioSystem::writeH8SToDSP(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_communicationMutex);
    m_h8sToDSPBuffer.push(data);
}

std::vector<uint8_t> MS2000AudioSystem::readDSPToH8S() {
    std::lock_guard<std::mutex> lock(m_communicationMutex);
    
    if (m_dspToH8SBuffer.empty()) {
        return {};
    }
    
    auto data = m_dspToH8SBuffer.front();
    m_dspToH8SBuffer.pop();
    return data;
}

void MS2000AudioSystem::writeDSPToCodec(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_communicationMutex);
    m_dspToCodecBuffer.push(data);
}

std::vector<uint8_t> MS2000AudioSystem::readCodecToDSP() {
    std::lock_guard<std::mutex> lock(m_communicationMutex);
    
    if (m_codecToDSPBuffer.empty()) {
        return {};
    }
    
    auto data = m_codecToDSPBuffer.front();
    m_codecToDSPBuffer.pop();
    return data;
}

void MS2000AudioSystem::setSystemClock(uint32_t frequency) {
    m_config.systemClock = frequency;
    synchronizeClocks();
}

void MS2000AudioSystem::setH8SClock(uint32_t frequency) {
    m_config.h8sClock = frequency;
    if (m_h8s) {
        // Update H8S clock frequency
    }
    synchronizeClocks();
}

void MS2000AudioSystem::setDSPClock(uint32_t frequency) {
    m_config.dspClock = frequency;
    if (m_dsp) {
        // Update DSP clock frequency
    }
    synchronizeClocks();
}

void MS2000AudioSystem::setFrameSync(bool active) {
    m_signalPath.frameSync = active;
    if (m_codec) {
        m_codec->setFrameSync(active);
    }
}

bool MS2000AudioSystem::isInputOverloaded() const {
    return m_codec ? m_codec->isInputOverloaded() : false;
}

bool MS2000AudioSystem::isOutputClipping() const {
    return m_codec ? m_codec->isOutputClipping() : false;
}

float MS2000AudioSystem::getInputLevel(uint8_t input) const {
    if (input < 2) {
        return m_codec ? m_codec->getInputLevel() : 0.0f;
    }
    return 0.0f;
}

float MS2000AudioSystem::getOutputLevel(uint8_t output) const {
    if (output < 3) {
        return m_codec ? m_codec->getOutputLevel() : 0.0f;
    }
    return 0.0f;
}

uint32_t MS2000AudioSystem::getActiveVoices() const {
    // This would be implemented based on DSP voice management
    return 0; // Placeholder
}

uint32_t MS2000AudioSystem::getAudioLatency() const {
    return m_audioOutput ? m_audioOutput->getLatency() : 0;
}

void MS2000AudioSystem::setAudioOutputCallback(std::function<void(const std::vector<float>&)> callback) {
    m_audioOutputCallback = callback;
}

void MS2000AudioSystem::setAudioInputCallback(std::function<void(const std::vector<float>&)> callback) {
    m_audioInputCallback = callback;
}

void MS2000AudioSystem::setDSPCommunicationCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    m_dspCommunicationCallback = callback;
}

void MS2000AudioSystem::setH8SCommunicationCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    m_h8sCommunicationCallback = callback;
}

void MS2000AudioSystem::initializeComponents() {
    // Initialize H8S/2350 emulator
    m_h8s = std::make_unique<H8S2350Emulator>();
    if (!m_h8s->initialize()) {
        throw std::runtime_error("Failed to initialize H8S/2350 emulator");
    }
    
    // Initialize DSP56362 emulator
    m_dsp = std::make_unique<DSP56362Emulator>();
    if (!m_dsp->initialize(m_config.dspClock, m_config.sampleRate)) {
        throw std::runtime_error("Failed to initialize DSP56362 emulator");
    }
    
    // Initialize AK4522VF codec
    AK4522VFConfig codecConfig;
    codecConfig.sampleRate = m_config.sampleRate;
    codecConfig.bitDepth = m_config.bitDepth;
    codecConfig.channels = 2;
    codecConfig.enableInput = m_config.enableInput;
    codecConfig.enableOutput = m_config.enableOutput;
    codecConfig.inputGain = 1.0f;
    codecConfig.outputGain = 1.0f;
    codecConfig.mute = m_mute;
    
    m_codec = std::make_unique<AK4522VFCodec>();
    if (!m_codec->initialize(codecConfig)) {
        throw std::runtime_error("Failed to initialize AK4522VF codec");
    }
    
    // Initialize audio output
    AudioConfig audioConfig;
    audioConfig.sampleRate = m_config.sampleRate;
    audioConfig.bufferSize = m_config.bufferSize;
    audioConfig.channels = 2;
    audioConfig.bitsPerSample = m_config.bitDepth;
    audioConfig.masterVolume = m_masterVolume;
    audioConfig.enableRealTime = m_config.enableRealTime;
    audioConfig.deviceName = m_config.audioDevice;
    
    m_audioOutput = std::make_unique<AudioOutput>();
    if (!m_audioOutput->initialize(audioConfig)) {
        throw std::runtime_error("Failed to initialize audio output");
    }
}

void MS2000AudioSystem::initializeAudioSignalPath() {
    // Initialize signal path buffers
    m_signalPath.input1Analog.reserve(m_config.bufferSize);
    m_signalPath.input2Analog.reserve(m_config.bufferSize);
    m_signalPath.codecToDSP.reserve(m_config.bufferSize * 6); // 24-bit stereo
    m_signalPath.dspToCodec.reserve(m_config.bufferSize * 6);
    m_signalPath.outputAnalog.reserve(m_config.bufferSize * 2); // Stereo
    
    // Set up codec callbacks
    m_codec->setInputCallback([this](const std::vector<AudioSample>& samples) {
        if (m_audioInputCallback) {
            std::vector<float> data;
            data.reserve(samples.size() * 2);
            for (const auto& sample : samples) {
                data.push_back(sample.left);
                data.push_back(sample.right);
            }
            m_audioInputCallback(data);
        }
    });
    
    m_codec->setOutputCallback([this](const std::vector<AudioSample>& samples) {
        if (m_audioOutputCallback) {
            std::vector<float> data;
            data.reserve(samples.size() * 2);
            for (const auto& sample : samples) {
                data.push_back(sample.left);
                data.push_back(sample.right);
            }
            m_audioOutputCallback(data);
        }
    });
}

void MS2000AudioSystem::initializeSerialCommunication() {
    // Set up serial communication between components
    // This would be implemented based on the actual serial protocols used
    // For now, we'll use the buffer-based approach
}

void MS2000AudioSystem::audioThreadFunction() {
    const auto frameInterval = std::chrono::microseconds(1000000 / m_config.sampleRate * m_config.bufferSize);
    
    while (m_threadRunning) {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        // Process audio frame
        processAudioFrame();
        
        // Wait for next frame
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = frameEnd - frameStart;
        
        if (frameDuration < frameInterval) {
            std::this_thread::sleep_for(frameInterval - frameDuration);
        }
    }
}

void MS2000AudioSystem::processInputPath() {
    // Process input amplifiers (InputVR1, InputVR2)
    updateInputAmplifiers();
    
    // Apply line/mic gain switch
    if (m_lineMicGainSwitch) {
        // Line input - normal gain
    } else {
        // Mic input - higher gain
        for (auto& sample : m_signalPath.input2Analog) {
            sample *= 10.0f; // Mic gain
        }
    }
}

void MS2000AudioSystem::processOutputPath() {
    // Process output amplifiers
    updateOutputAmplifiers();
    
    // Apply master volume
    updateMasterVolume();
    
    // Apply power-on mute
    if (m_powerOnMute) {
        for (auto& sample : m_signalPath.outputAnalog) {
            sample = 0.0f;
        }
    }
}

void MS2000AudioSystem::processDSPCommunication() {
    // Process DSP to codec communication
    std::lock_guard<std::mutex> lock(m_communicationMutex);
    
    while (!m_dspToCodecBuffer.empty()) {
        auto data = m_dspToCodecBuffer.front();
        m_dspToCodecBuffer.pop();
        
        m_codec->writeSDI(data);
        
        if (m_dspCommunicationCallback) {
            m_dspCommunicationCallback(data);
        }
    }
    
    // Process codec to DSP communication
    auto codecData = m_codec->readSDO();
    if (!codecData.empty()) {
        m_codecToDSPBuffer.push(codecData);
        
        if (m_dspCommunicationCallback) {
            m_dspCommunicationCallback(codecData);
        }
    }
}

void MS2000AudioSystem::processH8SCommunication() {
    // Process H8S to DSP communication
    std::lock_guard<std::mutex> lock(m_communicationMutex);
    
    while (!m_h8sToDSPBuffer.empty()) {
        auto data = m_h8sToDSPBuffer.front();
        m_h8sToDSPBuffer.pop();
        
        // Send to DSP (this would be implemented based on actual protocol)
        
        if (m_h8sCommunicationCallback) {
            m_h8sCommunicationCallback(data);
        }
    }
    
    // Process DSP to H8S communication
    while (!m_dspToH8SBuffer.empty()) {
        auto data = m_dspToH8SBuffer.front();
        m_dspToH8SBuffer.pop();
        
        // Send to H8S (this would be implemented based on actual protocol)
        
        if (m_h8sCommunicationCallback) {
            m_h8sCommunicationCallback(data);
        }
    }
}

void MS2000AudioSystem::updateInputAmplifiers() {
    // Update input amplifier gains based on control settings
    // This would be implemented based on actual amplifier characteristics
}

void MS2000AudioSystem::updateOutputAmplifiers() {
    // Update output amplifier gains based on control settings
    // This would be implemented based on actual amplifier characteristics
}

void MS2000AudioSystem::updateMasterVolume() {
    // Update master volume control
    if (m_audioOutput) {
        m_audioOutput->setMasterVolume(m_masterVolume);
    }
}

std::vector<uint8_t> MS2000AudioSystem::convertAnalogToDigital(const std::vector<float>& analog) {
    std::vector<uint8_t> digital;
    digital.reserve(analog.size() * (m_config.bitDepth / 8));
    
    for (float sample : analog) {
        uint32_t digitalValue = static_cast<uint32_t>((sample + 1.0f) * ((1 << m_config.bitDepth) - 1) / 2);
        
        // Convert to bytes
        if (m_config.bitDepth == 16) {
            digital.push_back((digitalValue >> 8) & 0xFF);
            digital.push_back(digitalValue & 0xFF);
        } else if (m_config.bitDepth == 24) {
            digital.push_back((digitalValue >> 16) & 0xFF);
            digital.push_back((digitalValue >> 8) & 0xFF);
            digital.push_back(digitalValue & 0xFF);
        }
    }
    
    return digital;
}

std::vector<float> MS2000AudioSystem::convertDigitalToAnalog(const std::vector<uint8_t>& digital) {
    std::vector<float> analog;
    size_t bytesPerSample = m_config.bitDepth / 8;
    
    for (size_t i = 0; i < digital.size(); i += bytesPerSample) {
        if (i + bytesPerSample > digital.size()) break;
        
        uint32_t digitalValue = 0;
        if (m_config.bitDepth == 16) {
            digitalValue = (digital[i] << 8) | digital[i + 1];
        } else if (m_config.bitDepth == 24) {
            digitalValue = (digital[i] << 16) | (digital[i + 1] << 8) | digital[i + 2];
        }
        
        float analogValue = (static_cast<float>(digitalValue) / ((1 << m_config.bitDepth) - 1)) * 2.0f - 1.0f;
        analog.push_back(analogValue);
    }
    
    return analog;
}

void MS2000AudioSystem::synchronizeClocks() {
    // Synchronize all clock domains
    // This would be implemented based on actual clock synchronization requirements
    
    // Update codec clocks
    if (m_codec) {
        m_codec->setSystemClock(m_config.systemClock);
        m_codec->set64fsClock(64 * m_config.sampleRate);
        m_codec->set256fsClock(256 * m_config.sampleRate);
    }
}

void MS2000AudioSystem::generateFrameSync() {
    // Generate frame sync signal for audio processing
    // This would be synchronized with the audio sample rate
    
    static uint64_t frameCounter = 0;
    frameCounter++;
    
    // Generate frame sync every buffer size samples
    if (frameCounter % m_config.bufferSize == 0) {
        setFrameSync(true);
        std::this_thread::sleep_for(std::chrono::microseconds(1)); // Brief pulse
        setFrameSync(false);
    }
}

void MS2000AudioSystem::detectInputLevels() {
    // Detect input levels for level indicators
    // This would update the level indicator LEDs
}

void MS2000AudioSystem::detectOutputLevels() {
    // Detect output levels for monitoring
    // This would update output level meters
}

void MS2000AudioSystem::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastError = error;
    std::cerr << "🎵 MS2000 Audio System Error: " << error << std::endl;
}

} // namespace MS2000
