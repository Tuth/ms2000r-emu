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
  AddVectoredExceptionHandler(1, StackOvfVeh);
  SetUnhandledExceptionFilter(SehFilter);
  { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }   // room for the filter after a stack overflow
#endif
  std::set_terminate(TerminateHandler);
}