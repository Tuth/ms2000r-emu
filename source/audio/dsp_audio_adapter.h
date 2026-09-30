#pragma once
#include "i2s_ak4522.h"
#include "../core/dsp56362_emulator.h"
#include <cmath>

namespace MS2000 {

class DspAudioAdapter {
public:
  DspAudioAdapter(Ak4522Bridge& ak, DSP56362Emulator& dsp) : ak_(ak), dsp_(dsp) {}
  
  void renderFrames(size_t n) {
    // 1 frame = 1 L/R mintapár @48k
    for (size_t i=0;i<n;i++) {
      // Kérj a DSP-től egy lépést (belső keverő/stub tone most oké)
      int16_t L=0, R=0;
      
      // TODO: Implement actual DSP audio step - for now generate 440Hz sine wave test tone
      static float phase = 0.0f;
      const float freq = 440.0f; // A4
      const float sr = 48000.0f;
      const float amp = 0.1f; // Low volume test tone
      
      float sample = amp * std::sin(2.0f * 3.14159265f * freq * phase);
      phase += 1.0f / sr;
      if (phase >= 1.0f) phase -= 1.0f;
      
      L = R = static_cast<int16_t>(sample * 32767.0f);
      
      // TODO: Replace with actual DSP processing:
      // dsp_.audioStep(L, R);   // implementáld: 1 mintapár szintézis
      
      ak_.push(L,R);
    }
  }
  
private:
  Ak4522Bridge& ak_;
  DSP56362Emulator& dsp_;
};

} // namespace MS2000