#include "audio_engine.h"
#include <thread>
#include <chrono>

bool AudioEngine::start(uint32_t sr, uint32_t bufferFrames,
                       std::function<void(size_t)> onProduce, 
                       Ak4522Bridge* ak) {
    if (running_.load()) return false;
    
    sampleRate_ = sr;
    bufferFrames_ = bufferFrames;
    onProduce_ = std::move(onProduce);
    ak_ = ak;
    
    running_.store(true);
    
    // For now, use a simple thread-based approach instead of WASAPI
    // This simulates the callback behavior that WASAPI would provide
    std::thread audioThread([this]() {
        std::vector<S16LR> buffer(bufferFrames_);
        auto nextFrame = std::chrono::steady_clock::now();
        auto frameDuration = std::chrono::microseconds(1000000 * bufferFrames_ / sampleRate_);
        
        while (running_.load()) {
            // Produce audio data
            if (onProduce_) {
                onProduce_(bufferFrames_);
            }
            
            // Pull from AK4522 bridge
            if (ak_) {
                size_t pulled = ak_->pull(buffer.data(), bufferFrames_);
                if (pulled < bufferFrames_) {
                    underruns_.fetch_add(1);
                }
                
                // pw_on.txt: Apply master volume/power-on control (post-DSP gain)
                if (pulled > 0) {
                    // Extract left/right channels for gain application  
                    std::vector<int16_t> left(pulled), right(pulled);
                    for (size_t i = 0; i < pulled; ++i) {
                        left[i] = buffer[i].L;
                        right[i] = buffer[i].R;
                    }
                    
                    // Apply master gain with smooth transitions
                    applyMasterGain(left.data(), right.data(), pulled);
                    
                    // Put back into buffer
                    for (size_t i = 0; i < pulled; ++i) {
                        buffer[i].L = left[i];
                        buffer[i].R = right[i];
                    }
                }
            }
            
            // Wait for next frame
            nextFrame += frameDuration;
            std::this_thread::sleep_until(nextFrame);
        }
    });
    
    audioThread.detach();
    return true;
}

void AudioEngine::stop() {
    running_.store(false);
    
    // Give some time for the audio thread to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

uint32_t AudioEngine::getCurrentLatencyMs() const {
    if (!ak_) return 0;
    return (1000 * bufferFrames_) / sampleRate_;
}

// pw_on.txt: Apply master gain with smooth 5ms slewing to avoid clicks/pops
void AudioEngine::applyMasterGain(int16_t* left, int16_t* right, size_t frames) {
    if (!left || !right || frames == 0) return;
    
    // 5ms time constant for smooth transitions
    const float tau = 0.005f;
    const float buffer_duration_sec = static_cast<float>(frames) / static_cast<float>(sampleRate_);
    const float coeff = 1.0f - std::exp(-buffer_duration_sec / tau);
    
    const float target = masterVolume_.getEffectiveTarget();
    
    for (size_t i = 0; i < frames; ++i) {
        // Smooth gain interpolation
        masterVolume_.current_gain += (target - masterVolume_.current_gain) * coeff;
        const float gain = masterVolume_.current_gain;
        
        // Apply gain with clipping (int16 range)
        int32_t l = static_cast<int32_t>(std::lrintf(left[i] * gain));
        int32_t r = static_cast<int32_t>(std::lrintf(right[i] * gain));
        
        left[i] = static_cast<int16_t>(std::clamp(l, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
        right[i] = static_cast<int16_t>(std::clamp(r, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
    }
}