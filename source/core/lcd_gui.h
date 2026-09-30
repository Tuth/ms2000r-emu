// lcd_gui.h — MS2000 2×16 LCD GUI buffer + bridge (fw12.txt ImGui integration)
#pragma once
#include <array>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <string>

struct LcdGuiSnapshot {
  char line0[17]{};
  char line1[17]{};
  bool displayOn=false, cursorOn=false, blinkOn=false;
  int  curLine=0, curCol=0; // 0..1, 0..15
  
  // fw14.txt: CGRAM custom character patterns (8 chars × 8 rows × 5 bits)
  uint8_t cgram[8][8]{}; // [character 0-7][row 0-7] = 5-bit pattern
};

class LcdGuiBuffer {
public:
  // --- BRIDGE: call these where you used to call hd44780::writeCmd/Data ---
  void onCmd(uint8_t c) {
    std::lock_guard<std::mutex> lk(m_);
    if ((c & 0x80) == 0x80) {             // Set DDRAM address
      ddramAddr_ = c & 0x7F;
      cgramMode_ = false;  // Switch to DDRAM mode
      return;
    }
    if ((c & 0x40) == 0x40) {             // fw14.txt: Set CGRAM address
      cgramAddr_ = c & 0x3F;              // 6-bit CGRAM address (0x40-0x7F)
      cgramMode_ = true;   // Switch to CGRAM mode
      return;
    }
    switch (c) {
      case 0x01: clearUnlocked(); break;  // Clear
      case 0x02: ddramAddr_ = 0; cgramMode_ = false; break;   // Home
      case 0x06: inc_ = true;  shift_ = false; break; // Entry Mode (simpl.)
      case 0x04: inc_ = false; shift_ = false; break;
      case 0x0C: displayOn_ = true; cursorOn_ = false; blinkOn_ = false; break;
      case 0x0E: displayOn_ = true; cursorOn_ = true;  blinkOn_ = false; break;
      case 0x0F: displayOn_ = true; cursorOn_ = true;  blinkOn_ = true;  break;
      case 0x08: displayOn_ = false; cursorOn_ = false; blinkOn_ = false; break;
      default: /* 0x38 etc. – ignore at viewer level */ break;
    }
  }

  void onData(uint8_t d) {
    std::lock_guard<std::mutex> lk(m_);
    
    if (cgramMode_) {
      // fw14.txt: Writing to CGRAM (custom character patterns)
      int charIndex = (cgramAddr_ >> 3) & 0x07;  // Characters 0-7
      int rowIndex = cgramAddr_ & 0x07;          // Rows 0-7
      if (charIndex < 8 && rowIndex < 8) {
        cgram_[charIndex][rowIndex] = d & 0x1F;  // Store 5-bit pattern
      }
      cgramAddr_ = (cgramAddr_ + 1) & 0x3F;      // Auto-increment CGRAM address
    } else {
      // Regular DDRAM (display) writing
      auto [ln, col] = mapAddr(ddramAddr_);
      if (ln>=0 && col>=0 && ln<2 && col<16) {
        // LCD-CG (2026-09-30, unknown's report: the tempo shows "=148", the unit "<note>=148"): the DDRAM holds
        // the character CODE, all 8 bits. 0x00-0x0F = the 8 CGRAM characters (bit 3 ignored, HD44780U data
        // sheet Table 4 - STATED, not re-read here), 0x10-0xFF = the CG ROM. The old filter kept only 0x00-0x07
        // and 0x20-0x7E and blanked the rest - the firmware's glyph came as another code and was lost.
        buf_[ln][col] = char(d);
      }
      stepAddr();
    }
    
    lastDataTs_ = nowMs();
  }

  // GUI thread: copy out atomically
  LcdGuiSnapshot snapshot() {
    std::lock_guard<std::mutex> lk(m_);
    LcdGuiSnapshot s{};
    for (int i=0;i<16;i++){ s.line0[i]=buf_[0][i]; s.line1[i]=buf_[1][i]; }
    s.line0[16]=0; s.line1[16]=0;
    s.displayOn = displayOn_; s.cursorOn = cursorOn_; s.blinkOn = blinkOn_;
    auto [ln, col] = mapAddr(ddramAddr_);
    s.curLine = ln; s.curCol = col;

    // fw14.txt: Copy CGRAM patterns
    for (int ch = 0; ch < 8; ch++) {
      for (int row = 0; row < 8; row++) {
        s.cgram[ch][row] = cgram_[ch][row];
      }
    }

    return s;
  }

  void reset() { std::lock_guard<std::mutex> lk(m_); clearUnlocked(); }

private:
  std::mutex m_;
  std::array<std::array<char,16>,2> buf_{{}};
  uint8_t ddramAddr_{0}; bool inc_{true}, shift_{false};
  bool displayOn_{false}, cursorOn_{false}, blinkOn_{false};
  uint64_t lastDataTs_{0};
  
  // fw14.txt: CGRAM support
  bool cgramMode_{false};         // true = writing to CGRAM, false = writing to DDRAM
  uint8_t cgramAddr_{0};          // CGRAM address (0x00-0x3F)
  uint8_t cgram_[8][8]{};         // Custom character patterns [char 0-7][row 0-7]

  static uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  void clearUnlocked() {
    for(auto& ln: buf_) ln.fill(' ');
    ddramAddr_ = 0;
    // fw14.txt: Clear CGRAM on display clear (optional behavior)
    cgramMode_ = false;
    cgramAddr_ = 0;
    // Note: Real HD44780 doesn't clear CGRAM on display clear,
    // but we can optionally do it for consistency
  }
  
  std::pair<int,int> mapAddr(uint8_t a) const {
    // HD44780 2×16: 0x00..0x0F = line 1; 0x40..0x4F = line 2
    if (a <= 0x0F) return {0, int(a)};
    if (a >= 0x40 && a <= 0x4F) return {1, int(a - 0x40)};
    // if FW sent 0x80/0xC0 (Set DDRAM), onCmd already masked it
    return {0,0};
  }
  
  void stepAddr() {
    if (inc_) ++ddramAddr_; else --ddramAddr_;
    // wrap within line
    if (ddramAddr_==0x10) ddramAddr_=0x40;
    if (ddramAddr_==0x50) ddramAddr_=0x00;
  }
};