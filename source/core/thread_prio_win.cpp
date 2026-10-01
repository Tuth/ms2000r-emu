// thread_prio_win.cpp - DSP-THREAD b (2026-10-01): an emulation worker thread as Windows treats an audio thread.
// Tamas measured: with the plugin's DSP thread, total CPU rose and jumped further when the host went to the
// background. Windows 11 throttles the threads of a background process (EcoQoS: lower clock / efficiency cores)
// unless they opt out; the host's own audio thread is registered with MMCSS, a plugin's own thread is not. So the
// worker (1) joins MMCSS "Pro Audio" and (2) opts out of execution-speed power throttling.
#ifdef _WIN32
#define NOMINMAX
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // SetThreadInformation (Windows 8+)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")

namespace MS2000 {
void ms2kAudioWorkerThread()
{
    DWORD idx = 0;
    AvSetMmThreadCharacteristicsW(L"Pro Audio", &idx);
    THREAD_POWER_THROTTLING_STATE st{};
    st.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = 0;                                    // throttling OFF for this thread
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &st, sizeof st);
}
}
#else
namespace MS2000 { void ms2kAudioWorkerThread() {} }
#endif
