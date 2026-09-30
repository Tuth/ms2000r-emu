#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include "mmcss_audio.h"

class AudioThread {
public:
  using Callback = std::function<void()>; // pl. 128 mintás blokk pumpálása

  bool start(Callback cb, bool useMmcss) {
    if (running_) return true;
    running_ = true;
    th_ = std::thread([=]{
      MmcssHandle mm;
      if (useMmcss) enable_mmcss(mm, L"Pro Audio", AVRT_PRIORITY_CRITICAL);

      // Opcionális: emelt normál prio fallback (nem szükséges MMCSS mellé)
      // SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

      // Fő render loop – 2–3 ms-os blokkokkal (pl. 128@48k ≈ 2.67ms)
      while (running_) {
        cb();                          // pumpáld a következő audioblokkot
        Sleep(1);                      // kíméletes yield – MMCSS miatt elég
      }
      disable_mmcss(mm);
    });
    return true;
  }

  void stop() {
    if (!running_) return;
    running_ = false;
    if (th_.joinable()) th_.join();
  }

  ~AudioThread(){ stop(); }

private:
  std::thread th_;
  std::atomic<bool> running_{false};
};