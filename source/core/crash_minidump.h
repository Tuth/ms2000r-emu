#pragma once
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")

// From crash_dump.h
extern std::string make_ts_path(const char* dir, const char* base, const char* ext);

static inline bool write_minidump(const char* dir, EXCEPTION_POINTERS* ep=nullptr){
  auto path = make_ts_path(dir, "crash", "dmp");
  HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile==INVALID_HANDLE_VALUE) return false;

  MINIDUMP_EXCEPTION_INFORMATION mei{};
  mei.ThreadId = GetCurrentThreadId();
  mei.ExceptionPointers = ep;
  mei.ClientPointers = FALSE;

  BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
    static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory), ep ? &mei : nullptr, nullptr, nullptr);

  CloseHandle(hFile);
  return ok==TRUE;
}
#endif // _WIN32