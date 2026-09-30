// lcd_gui_imgui.h — ImGui renderer for MS2000 LCD widget (fw12.txt)
#pragma once
#include "lcd_gui.h"
#include "imgui.h"

// fw14.txt: Helper to render custom CGRAM character as bitmap
inline void DrawCustomChar(ImDrawList* dl, const ImVec2& pos, uint8_t charIndex, const LcdGuiSnapshot& s, unsigned int color) {
  if (charIndex >= 8) return;  // Only 8 custom characters (0-7)
  
  const float pixelSize = 2.0f;  // Size of each pixel in the 5×8 character
  const float charWidth = 5 * pixelSize;
  const float charHeight = 8 * pixelSize;
  
  // Draw background
  dl->AddRectFilled(pos, ImVec2(pos.x + charWidth, pos.y + charHeight), IM_COL32(40, 80, 40, 255));
  
  // Draw character pattern
  for (int row = 0; row < 8; row++) {
    uint8_t pattern = s.cgram[charIndex][row] & 0x1F;  // 5 bits
    for (int col = 0; col < 5; col++) {
      if (pattern & (1 << (4 - col))) {  // MSB first
        float px = pos.x + col * pixelSize;
        float py = pos.y + row * pixelSize;
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pixelSize, py + pixelSize), color);
      }
    }
  }
}

// fw14.txt: Enhanced LCD renderer with CGRAM support
inline void DrawLcdImGui(const LcdGuiSnapshot& s) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
  
  if (!ImGui::Begin("MS2000 LCD")) {
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  // Frame + "backlight"
  auto* dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 size(16*16.0f + 24.0f, 2*22.0f + 24.0f); // char width × 16 + padding
  ImVec2 p1(p0.x+size.x, p0.y+size.y);
  dl->AddRectFilled(p0, p1, IM_COL32(40, 80, 40, 255));   // greenish backlight
  dl->AddRect(p0, p1, IM_COL32(20, 40, 20, 255), 8.0f, 0, 2.0f);
  ImGui::Dummy(size); 
  ImGui::SetCursorScreenPos(ImVec2{p0.x+12, p0.y+10});

  // fw14.txt: Enhanced character rendering with CGRAM support
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230,255,230,255));
  ImGui::SetWindowFontScale(1.2f);

  // fw28.txt: FORCE DISPLAY ON - Override the displayOn check for debugging
  // This will force the GUI to always show LCD content regardless of displayOn value
  // if (!s.displayOn) {
  if (false) {  // Force this condition to always be false
    ImGui::TextDisabled("(display off)");
    char debugMsg[64];
    sprintf_s(debugMsg, sizeof(debugMsg), "Debug: displayOn=%s", s.displayOn ? "true" : "false");
    ImGui::TextUnformatted(debugMsg);

  } else {

    // Render line 0 with custom character support
    for (int col = 0; col < 16; col++) {
      char ch = s.line0[col];
      float x = p0.x + 12 + col * 16.0f;
      float y = p0.y + 10;

      if (ch >= 0 && ch < 8) {
        // Custom CGRAM character
        DrawCustomChar(dl, ImVec2(x, y), (uint8_t)ch, s, IM_COL32(230, 255, 230, 255));
      } else {
        // Regular ASCII character - use ImGui text rendering
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        char single[2] = {ch, '\0'};
        ImGui::TextUnformatted(single);
      }
    }
    
    // Render line 1 with custom character support
    for (int col = 0; col < 16; col++) {
      char ch = s.line1[col];
      float x = p0.x + 12 + col * 16.0f;
      float y = p0.y + 10 + 22;
      
      if (ch >= 0 && ch < 8) {
        // Custom CGRAM character
        DrawCustomChar(dl, ImVec2(x, y), (uint8_t)ch, s, IM_COL32(230, 255, 230, 255));
      } else {
        // Regular ASCII character - use ImGui text rendering
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        char single[2] = {ch, '\0'};
        ImGui::TextUnformatted(single);
      }
    }

    // Cursor/blink
    if (s.cursorOn) {
      static uint64_t t0=0; 
      uint64_t t = (uint64_t)(ImGui::GetTime()*1000.0);
      bool blinkOn = s.blinkOn ? ((t/500)%2==0) : true;
      if (blinkOn && s.curLine>=0 && s.curCol>=0 && s.curCol<16) {
        float x = p0.x + 12 + s.curCol * 16.0f;
        float y = p0.y + 10 + (s.curLine==0? 16.0f: 38.0f);
        dl->AddRectFilled(ImVec2{x, y}, ImVec2{x+12, y+2}, IM_COL32(230,255,230,255));
      }
    }
  }
  
  ImGui::PopStyleColor();
  ImGui::End();
  ImGui::PopStyleVar();
}