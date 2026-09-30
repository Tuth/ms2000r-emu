#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <ctime>
#include <vector>

// -- Ezek a te projektedből jönnek:
#include "diag_panel.h"   // DiagSnapshot (csak POD tagokat használjunk benne!)
#include "lcd_gui.h"      // LcdGuiSnapshot (POD)

// Forward declarations - these will be defined elsewhere
extern DiagState     g_diag;
extern LcdGuiBuffer  g_lcdGui;

// ---- LOG tail (opcionális) ----
#ifndef CRASH_LOG_TAIL_MAX
#define CRASH_LOG_TAIL_MAX 256
#endif

struct CrashLogTail {
  uint32_t count=0;             // használt bejegyzések
  char     lines[CRASH_LOG_TAIL_MAX][120]{}; // utolsó rövid log sorok
};

// Töltsd a globális loggeredben (fw20) – lásd lejjebb
extern CrashLogTail g_crashLogTail;

// Helper functions
void CrashTailAppend(const char* line);
void MpTailAppend(uint8_t byte);

// ---- MP tail (PanelMP körpuffer linearizáláshoz) ----
struct MpTail256 {
  uint8_t buf[256]{}; uint32_t w=0; // körpuffer, w növekvő
};

extern MpTail256 g_mpTail;  // PanelMP-ben töltsd: g_mpTail.buf[g_mpTail.w++ & 255] = b;

static inline void linearize_mptail(const MpTail256& t, std::vector<uint8_t>& out){
  out.clear();
  uint32_t len = t.w < 256 ? t.w : 256;
  if (len==0) return;
  uint32_t start = (t.w >= 256) ? (t.w & 255) : 0;
  for (uint32_t i=0;i<len;i++){
    uint32_t idx = (start + i) & 255;
    out.push_back(t.buf[idx]);
  }
}

// ---- Bináris dump fej + tartalom ----
#pragma pack(push,1)
struct CrashDumpHeader {
  char     magic[8];      // "MS2KDMP\0"
  uint32_t version;       // 2
  uint64_t ts_unix;       // epoch sec
  uint32_t blob_size;     // teljes fájlméret bytes
};

struct CrashDumpV2 {
  CrashDumpHeader hdr;

  // Rövid, platformfüggetlen másolatok (POD)
  DiagSnapshot   diag;
  LcdGuiSnapshot lcd;

  // MP tail (linearizált, max 256)
  uint32_t mp_len;
  uint8_t  mp_tail[256];

  // Log tail
  CrashLogTail log_tail;
};
#pragma pack(pop)

// ---- Útvonalépítő ----
static inline std::string make_ts_path(const char* dir, const char* base, const char* ext){
  std::time_t t = std::time(nullptr);
  std::tm tm{}; 
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char name[256];
  std::snprintf(name, sizeof(name), "%s\\%s_%04d%02d%02d_%02d%02d%02d.%s",
    dir, base, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ext);
  return name;
}

// ---- Dump író (bináris) ----
static inline bool write_crash_dump_bin(const char* dir){
  CrashDumpV2 d{};
  std::memcpy(d.hdr.magic, "MS2KDMP", 7);
  d.hdr.magic[7] = 0;
  d.hdr.version  = 2;
  d.hdr.ts_unix  = (uint64_t)std::time(nullptr);

  d.diag = g_diag.snapshot();
  d.lcd  = g_lcdGui.snapshot();

  std::vector<uint8_t> mpt;
  linearize_mptail(g_mpTail, mpt);
  d.mp_len = (uint32_t)std::min<size_t>(mpt.size(), 256);
  if (d.mp_len) std::memcpy(d.mp_tail, mpt.data(), d.mp_len);

  // Log tail beszúrása (g_crashLogTail-t feltöltöd a loggerben)
  d.log_tail = g_crashLogTail;

  d.hdr.blob_size = (uint32_t)sizeof(d);
  auto path = make_ts_path(dir, "crash", "bin");
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "wb") == 0 && f != nullptr){
    std::fwrite(&d, sizeof(d), 1, f);
    std::fclose(f);
    return true;
  }
  return false;
}