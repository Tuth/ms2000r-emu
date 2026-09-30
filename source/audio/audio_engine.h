#pragma once
#include "i2s_ak4522.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

// pw_on.txt: Master Volume/Power-On control (post-DSP gain)
struct MasterVolume {
    std::atomic<float> target_gain{0.0f};   // 0..1 (linear, starts muted)
    float current_gain = 0.0f;              // smoothed current gain
    std::atomic<bool> power_on{false};      // power switch state
    bool mute = true;                       // mute state (starts muted)
    
    // Convert slider percentage (0-100) to linear gain with log taper
    static float percent_to_gain(float percent) {
        float v01 = std::clamp(percent / 100.0f, 0.0f, 1.0f);
        float db = -80.0f + 80.0f * v01;  // -80dB to 0dB range
        return std::pow(10.0f, db / 20.0f);
    }
    
    // Set master volume percentage (0-100%)
    void setPercent(float percent) {
        target_gain.store(percent_to_gain(percent), std::memory_order_relaxed);
    }
    
    // Power-on sequence: unmute and fade in (with delay)
    void powerOn() {
        power_on.store(true, std::memory_order_relaxed);
        // Start with 80ms delay before unmuting (as per pw_on.txt)
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            mute = false;
        }).detach();
    }
    
    // Power-off sequence: fade out and mute
    void powerOff() {
        power_on.store(false, std::memory_order_relaxed);  
        mute = true;
    }
    
    // Get effective target (respects power/mute state)
    float getEffectiveTarget() const {
        if (mute || !power_on.load(std::memory_order_relaxed)) {
            return 0.0f;
        }
        return target_gain.load(std::memory_order_relaxed);
    }
};

class AudioEngine {
public:
  bool start(uint32_t sr, uint32_t bufferFrames,
             std::function<void(size_t)> onProduce, // DSP→AK: ennyi frame-t állíts elő
             Ak4522Bridge* ak);
  void stop();
  
  bool isRunning() const { return running_.load(); }
  
  // Statistics
  uint64_t getUnderruns() const { return underruns_.load(); }
  uint32_t getCurrentLatencyMs() const;
  
  // pw_on.txt: Master Volume/Power-On control
  void setMasterVolumePercent(float percent) { masterVolume_.setPercent(percent); }
  void setPowerOn(bool on) { 
    if (on) {
      masterVolume_.powerOn();
    } else {
      masterVolume_.powerOff();
    }
  }
  bool isPowerOn() const { return masterVolume_.power_on.load(); }
  void setMute(bool mute) { masterVolume_.mute = mute; }
  bool isMuted() const { return masterVolume_.mute; }
  
private:
  // WASAPI init: shared mode, float32 or int16; itt int16 a legegyszerűbb
  // Callback: kér n frame-t → onProduce(n) → ak->pull(out, n)
  
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> underruns_{0};
  
  uint32_t sampleRate_ = 48000;
  uint32_t bufferFrames_ = 256;
  std::function<void(size_t)> onProduce_;
  Ak4522Bridge* ak_ = nullptr;
  
  // pw_on.txt: Master Volume control instance  
  MasterVolume masterVolume_;
  
  // pw_on.txt: Apply master gain with smooth transitions (called in audio callback)
  void applyMasterGain(int16_t* left, int16_t* right, size_t frames);
  
  // Platform-specific implementation details
#ifdef _WIN32
  // WASAPI implementation details will go here
  void* audioClient_ = nullptr;  // IAudioClient*
  void* renderClient_ = nullptr; // IAudioRenderClient*
#endif
};