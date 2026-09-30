#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

template<typename T>
class SpscRing {
public:
  explicit SpscRing(size_t cap_pow2) : mask_(cap_pow2-1), q_(cap_pow2) {}
  bool push(const T& v) {
    auto h = head_.load(std::memory_order_relaxed);
    auto n = (h + 1) & mask_;
    if (n == tail_.load(std::memory_order_acquire)) return false; // full
    q_[h] = v;
    head_.store(n, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    auto t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false; // empty
    out = q_[t];
    tail_.store((t+1)&mask_, std::memory_order_release);
    return true;
  }
  size_t size() const {
    auto h=head_.load(std::memory_order_acquire), t=tail_.load(std::memory_order_acquire);
    return (h - t) & mask_;
  }
  size_t capacity() const { return q_.size()-1; }
private:
  std::atomic<size_t> head_{0}, tail_{0};
  size_t mask_;
  std::vector<T> q_;
};

// Sample formátum
struct S16LR { int16_t L, R; };