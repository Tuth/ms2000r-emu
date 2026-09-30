#pragma once
#include "spsc_ring.h"
#include <cstdint>

class Ak4522Bridge {
public:
  bool init(uint32_t sampleRate) { sr_ = sampleRate; return true; }
  void reset() { xruns_=0; }
  // DSP → CODEC: egy mintapár
  inline void push(int16_t l, int16_t r) {
    if (!rb_.push({l,r})) xruns_++;
  }
  // Host → kimeneti buffer: tölts fel N mintapárt
  size_t pull(S16LR* out, size_t n) {
    size_t i=0; for (; i<n; ++i) if (!rb_.pop(out[i])) break;
    // underrun esetén tölts nullával a maradékra
    for (size_t j=i; j<n; ++j) out[j] = S16LR{0,0};
    return i;
  }
  size_t fill() const { return rb_.size(); }
  size_t cap()  const { return rb_.capacity(); }
  uint64_t xruns() const { return xruns_; }

private:
  uint32_t sr_=48000;
  SpscRing<S16LR> rb_{2048}; // 2048 mintapár = 42.6 ms @48k
  std::atomic<uint64_t> xruns_{0};
};