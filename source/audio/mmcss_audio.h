#pragma once
#ifndef _WIN32
// LINUX-1: MMCSS is Windows-only; elsewhere the audio thread runs at normal priority.
#include <chrono>
#include <thread>
enum AVRT_PRIORITY { AVRT_PRIORITY_CRITICAL };
struct MmcssHandle {};
inline bool enable_mmcss(MmcssHandle&, const wchar_t* = L"Pro Audio", AVRT_PRIORITY = AVRT_PRIORITY_CRITICAL) { return false; }
inline void disable_mmcss(MmcssHandle&) {}
inline void Sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#else
#include <windows.h>
#include <avrt.h>
#include <mmsystem.h>
#pragma comment(lib, "Avrt.lib")
#pragma comment(lib, "Winmm.lib")

struct MmcssHandle {
  HANDLE taskHandle = nullptr;
  DWORD  taskIndex  = 0;
  bool   timePeriod = false;
};

inline bool enable_mmcss(MmcssHandle& h, const wchar_t* task=L"Pro Audio",
                         AVRT_PRIORITY prio=AVRT_PRIORITY_CRITICAL)
{
  // Finomabb időzítés (GUI-tól függetlenül)
  if (timeBeginPeriod(1) == TIMERR_NOERROR) h.timePeriod = true;

  // Csatlakozás MMCSS-hez
  h.taskHandle = AvSetMmThreadCharacteristicsW(task, &h.taskIndex);
  if (!h.taskHandle) return false;

  // Emelt MMCSS prioritás
  if (!AvSetMmThreadPriority(h.taskHandle, prio)) {
    AvRevertMmThreadCharacteristics(h.taskHandle);
    h.taskHandle = nullptr;
    return false;
  }
  return true;
}

inline void disable_mmcss(MmcssHandle& h)
{
  if (h.taskHandle) {
    AvRevertMmThreadCharacteristics(h.taskHandle);
    h.taskHandle = nullptr;
  }
  if (h.timePeriod) {
    timeEndPeriod(1);
    h.timePeriod = false;
  }
}
#endif
