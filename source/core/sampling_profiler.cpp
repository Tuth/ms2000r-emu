// PERF-124: MS2K_PROFILE=1 - a sampling profiler for the CPU thread (measurement only, default OFF).
// Every ~0.5 ms the CPU thread is suspended, its RIP read, and resumed. At shutdown the samples are
// written to ms2k_profile.txt as "count rva" lines (rva = RIP - image base); tools/prof_resolve.py
// maps them to functions with the linker map (ms2000_emulator.map, /MAP).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <unordered_map>

namespace {
std::atomic<bool> g_profRun{false};
std::thread g_profThread;
std::unordered_map<uint64_t, uint64_t> g_hist;
uint64_t g_samples = 0;
}

void ms2k_profiler_start(void* nativeThreadHandle)
{
    const char* e = std::getenv("MS2K_PROFILE");
    if (!e || *e != '1' || g_profRun.load()) return;
    HANDLE h = static_cast<HANDLE>(nativeThreadHandle);
    g_profRun = true;
    g_profThread = std::thread([h] {
        const uint64_t base = uint64_t(GetModuleHandleW(nullptr));
        timeBeginPeriod(1);
        // MS2K_PROFILE_DELAY=<wall s>: start sampling late (skip boot, JIT warm-up, wave upload)
        if (const char* d = std::getenv("MS2K_PROFILE_DELAY")) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(long(std::atof(d) * 1000));
            while (g_profRun.load() && std::chrono::steady_clock::now() < until) Sleep(10);
        }
        while (g_profRun.load(std::memory_order_relaxed)) {
            if (SuspendThread(h) != DWORD(-1)) {
                CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(h, &c)) { g_hist[uint64_t(c.Rip) - base]++; ++g_samples; }
                ResumeThread(h);
            }
            const auto t = std::chrono::steady_clock::now() + std::chrono::microseconds(250);
            while (std::chrono::steady_clock::now() < t) SwitchToThread();   // ~4 kHz, this thread burns a core
        }
        timeEndPeriod(1);
    });
    printf("[PROFILE] sampling the CPU thread (MS2K_PROFILE=1)\n");
}

void ms2k_profiler_stop()
{
    if (!g_profRun.exchange(false)) return;
    if (g_profThread.joinable()) g_profThread.join();
    FILE* f = std::fopen("ms2k_profile.txt", "w");
    if (!f) return;
    std::fprintf(f, "# samples %llu\n", (unsigned long long)g_samples);
    for (auto& kv : g_hist) std::fprintf(f, "%llu %llx\n", (unsigned long long)kv.second, (unsigned long long)kv.first);
    std::fclose(f);
    printf("[PROFILE] %llu samples -> ms2k_profile.txt\n", (unsigned long long)g_samples);
}
#else
void ms2k_profiler_start(void*) {}
void ms2k_profiler_stop() {}
#endif
