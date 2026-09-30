#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <thread>
#include "../audio/spsc_ring.h" // már megvan

// Forward declaration for crash tail collection
void CrashTailAppend(const char* line);

enum class LogLevel : uint8_t { Trace, Debug, Info, Warn, Error };

struct LogEvent {
  LogLevel lvl;
  uint64_t ts_ms;
  char     msg[256];
};

class AsyncLogger {
public:
  bool start(size_t qcap_pow2=4096, uint32_t max_lps=200) {
    maxLinesPerSec_ = max_lps;
    // BUG101b: this is a process-wide logger, and a second Ms2kRunner in the same
    // process called start() again. Move-assigning a new std::thread onto a
    // JOINABLE one is std::terminate by the C++ standard - that is the
    // "[CRASH] std::terminate" BUG69 saw, and the one a fresh Golden Master
    // machine hit on 2026-09-23. Already running = nothing to start.
    if (worker_.joinable()) return true;
    running_ = true;
    worker_ = std::thread([&]{ loop(); });
    return true;
  }
  void stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
  }
  ~AsyncLogger(){ stop(); }

  void logf(LogLevel lvl, const char* fmt, ...) {
    LogEvent ev{};
    ev.lvl = lvl;
    ev.ts_ms = nowMs();
    va_list ap; va_start(ap, fmt);
    vsnprintf(ev.msg, sizeof(ev.msg), fmt, ap);
    va_end(ap);
    if (!q_.push(ev)) dropped_.fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
  SpscRing<LogEvent> q_{4096};
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> dropped_{0};
  uint32_t maxLinesPerSec_ = 200;

  static uint64_t nowMs(){
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  void loop(){
    using clk = std::chrono::steady_clock;
    auto windowStart = clk::now();
    uint32_t emitted = 0;

    while (running_) {
      LogEvent ev;
      if (!q_.pop(ev)) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

      // rate-window
      auto now = clk::now();
      if (std::chrono::duration_cast<std::chrono::seconds>(now - windowStart).count() >= 1) {
        windowStart = now; emitted = 0;
      }
      if (emitted >= maxLinesPerSec_) {
        // ha túlcsordul, "coalesce": csak minden 10. sort engedj át
        static uint32_t skip=0; if ((++skip % 10)!=0) continue;
      }
      emitted++;

      // kiírás (stderr), egyszerű prefix
      const char* L = ev.lvl==LogLevel::Trace?"T":
                      ev.lvl==LogLevel::Debug?"D":
                      ev.lvl==LogLevel::Info ?"I":
                      ev.lvl==LogLevel::Warn ?"W":"E";
      
      // Add to crash tail for dump collection
      CrashTailAppend(ev.msg);
      
      std::fprintf(stderr, "[%s] %s\n", L, ev.msg);
    }
  }
};

namespace MS2000 { extern AsyncLogger g_log; }
inline void LOGI(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  char b[256]; vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  MS2000::g_log.logf(LogLevel::Info, "%s", b);
}
inline void LOGE(const char* fmt, ...) { 
  va_list ap; va_start(ap, fmt); 
  char b[256]; vsnprintf(b,sizeof(b),fmt,ap); va_end(ap); 
  MS2000::g_log.logf(LogLevel::Error, "%s", b); 
}