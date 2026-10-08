#include "crash_handlers.h"
#include "crash_dump.h"
#ifdef _WIN32
  #include "crash_minidump.h"
  #include <windows.h>
#endif
#include <exception>
#include <atomic>
#include <iostream>

static const char* g_crashDir = "dumps";
static void (*g_crashHook)() = nullptr;   // VOCODER-1: a subsystem's last-words report (R2, set only by diagnostics)
void SetCrashReportHook(void (*f)()){ g_crashHook = f; }
#ifdef _WIN32
// 2026-09-26: a vectored handler sees a stack overflow first, on the faulting thread (with the stack
// guarantee below), before anything else can swallow it.
static LONG WINAPI StackOvfVeh(EXCEPTION_POINTERS* ep){
  if (!ep || !ep->ExceptionRecord || ep->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  char line[96];
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE); DWORD w = 0;
  int k = std::snprintf(line, sizeof line, "[CRASH] STACK OVERFLOW at rva %llX\n", (unsigned long long)(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) - base));
  WriteFile(h, line, DWORD(k), &w, nullptr);
  void* fr[40]; const USHORT n = RtlCaptureStackBackTrace(0, 40, fr, nullptr);
  for (USHORT i = 0; i < n; ++i) { k = std::snprintf(line, sizeof line, "[CRASH] frame %2u rva %llX\n", unsigned(i), (unsigned long long)(reinterpret_cast<uintptr_t>(fr[i]) - base)); WriteFile(h, line, DWORD(k), &w, nullptr); }
  if (g_crashHook) g_crashHook();
  TerminateProcess(GetCurrentProcess(), 0xC00000FD);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
static std::atomic<bool> g_dumping{false};

// 2026-10-08 UNLOAD CRASH: InstallCrashHandlers registers PROCESS-WIDE handlers. In the plugin this module is a DLL
// (MS2000R_engine.dll) that the host unloads; the vectored handler stayed registered and the next exception anywhere
// in the host called StackOvfVeh in freed memory - Event Log: faulting module MS2000R_engine.dll_unloaded, offset
// 0x3a0f0 = the first instruction of StackOvfVeh (VSTHost and Cubase 12 alike). Keep the handles, remove them again.
#ifdef _WIN32
static PVOID g_veh = nullptr;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
static bool g_filterSet = false;
#endif
static std::terminate_handler g_prevTerminate = nullptr;
static bool g_terminateSet = false;

#ifdef _WIN32
static LONG WINAPI SehFilter(EXCEPTION_POINTERS* ep){
  // 2026-09-26: a stack overflow killed the process without a word. Print where: the faulting RIP and
  // the return addresses on the stack, as RVAs of the exe (resolve with the /MAP file, tools/prof_resolve.py).
  if (ep && ep->ExceptionRecord) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[CRASH] exception 0x%08lX at rva %llX\n", (unsigned long)ep->ExceptionRecord->ExceptionCode,
                 (unsigned long long)(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) - base));
    void* fr[48]; const USHORT n = RtlCaptureStackBackTrace(0, 48, fr, nullptr);
    for (USHORT i = 0; i < n; ++i) std::fprintf(stderr, "[CRASH] frame %2u rva %llX\n", unsigned(i), (unsigned long long)(reinterpret_cast<uintptr_t>(fr[i]) - base));
    std::fflush(stderr);
  }
  if (!g_dumping.exchange(true)) {
    std::fprintf(stderr, "[CRASH] Writing crash dump to %s\n", g_crashDir);
    write_crash_dump_bin(g_crashDir);
    write_minidump(g_crashDir, ep);
    std::fprintf(stderr, "[CRASH] Crash dump written\n");
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void TerminateHandler(){
  if (!g_dumping.exchange(true)) {
    std::fprintf(stderr, "[CRASH] std::terminate - Writing crash dump to %s\n", g_crashDir);
    write_crash_dump_bin(g_crashDir);
#ifdef _WIN32
    write_minidump(g_crashDir, nullptr);
#endif
    std::fprintf(stderr, "[CRASH] Crash dump written\n");
  }
  std::abort();
}

void InstallCrashHandlersThisThread(){
#ifdef _WIN32
  ULONG g = 64 * 1024; SetThreadStackGuarantee(&g);
#endif
}

void InstallCrashHandlers(const char* dumpDir){
  g_crashDir = dumpDir ? dumpDir : "dumps";
  
  std::fprintf(stderr, "[CRASH] Installing crash handlers, dump dir: %s\n", g_crashDir);
  
#ifdef _WIN32
  if (!g_veh) g_veh = AddVectoredExceptionHandler(1, StackOvfVeh);
  if (!g_filterSet) { g_prevFilter = SetUnhandledExceptionFilter(SehFilter); g_filterSet = true; }
  { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }   // room for the filter after a stack overflow
#endif
  if (!g_terminateSet) { g_prevTerminate = std::set_terminate(TerminateHandler); g_terminateSet = true; }
}

void UninstallCrashHandlers(){
#ifdef _WIN32
  if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = nullptr; }
  if (g_filterSet) {
    // restore the previous filter only if ours is still the active one; a filter installed after ours is left alone
    const auto cur = SetUnhandledExceptionFilter(g_prevFilter);
    if (cur != SehFilter) SetUnhandledExceptionFilter(cur);
    g_filterSet = false;
  }
#endif
  if (g_terminateSet) {
    if (std::get_terminate() == TerminateHandler) std::set_terminate(g_prevTerminate);
    g_terminateSet = false;
  }
}

// Static destructors of a DLL run on FreeLibrary, before its code is unmapped: the handlers can never outlive the module.
namespace { struct CrashHandlerUnloadGuard { ~CrashHandlerUnloadGuard(){ UninstallCrashHandlers(); } } g_crashHandlerUnloadGuard; }