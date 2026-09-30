#pragma once
#include <cstdint>
#include <chrono>

struct LcdBootProbe {
  bool got_fn=false, got_on=false, got_clr=false, got_ent=false;
  bool got_ddram0=false;  // 0x80 vagy 0xC0 beállítva
  int  data_chars=0;

  std::chrono::steady_clock::time_point t0{std::chrono::steady_clock::now()};
  std::chrono::steady_clock::time_point last_cmd{}, last_data{};

  // GATE flags (Runner töltse)
  bool g_dsp_ack=false, g_codec_unmuted=false, g_panel_ok=false, g_tick_ok=false, g_vbr_ok=true;

  void on_cmd(uint8_t c) {
    last_cmd = std::chrono::steady_clock::now();
    if (c==0x38) got_fn=true;
    else if ((c & 0xFE)==0x0C) got_on=true;    // 0x0C/0x0D
    else if (c==0x01) got_clr=true;
    else if (c==0x06) got_ent=true;
    else if ((c & 0x80)==0x80) got_ddram0=true; // DDRAM addr set (0x80..)
  }
  void on_data(uint8_t) {
    last_data = std::chrono::steady_clock::now();
    data_chars++;
  }
};