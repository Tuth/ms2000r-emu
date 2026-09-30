#include "crash_dump.h"
#include "diag_panel.h"
#include "lcd_gui.h"
#include <cstring>

// Global crash dump data collectors
CrashLogTail g_crashLogTail;
MpTail256 g_mpTail;

// Forward declared globals needed by crash dump system
DiagState g_diag;
LcdGuiBuffer g_lcdGui;

void CrashTailAppend(const char* line){
  if (!line) return;
  
  // egyszerű ring: shiftelj, majd beszúrj a végére
  if (g_crashLogTail.count < CRASH_LOG_TAIL_MAX) {
    std::snprintf(g_crashLogTail.lines[g_crashLogTail.count++], 120, "%s", line);
  } else {
    // balra shift (fix 256*120 memmove ~31KB; ritka → oké)
    for (uint32_t i=1;i<CRASH_LOG_TAIL_MAX;i++)
      std::memcpy(g_crashLogTail.lines[i-1], g_crashLogTail.lines[i], 120);
    std::snprintf(g_crashLogTail.lines[CRASH_LOG_TAIL_MAX-1], 120, "%s", line);
  }
}

void MpTailAppend(uint8_t byte){
  g_mpTail.buf[g_mpTail.w++ & 255] = byte;
}