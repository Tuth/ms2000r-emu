#pragma once
#include <functional>
#include <string>
#include <cstdint>

// fw28.txt: Win32/DX11 GUI infrastructure for MS2000 Emulator
// GPT5 Testvér GUI skeleton implementation

// Forward declarations
namespace MS2000 { class Ms2kRunner; }
struct LcdGuiSnapshot;

struct AppConfig {
    bool vsync = true;
    int width = 1024;
    int height = 640;
    std::string title = "KORG MS2000 Emulator - fw28.txt GUI";
};

// Main GUI entry point (Win32 + DX11 + ImGui)
int run_gui_dx11(MS2000::Ms2kRunner& runner, const AppConfig& cfg);

// Panel control widgets
void DrawPanelControls(
    const std::function<void(uint8_t/*swId*/, bool)>& sendSwitch,
    const std::function<void(uint8_t/*ledId*/, bool)>& sendLed,
    const std::function<void(uint8_t/*vrId*/, float /*norm01*/)>& sendVr
);

// Audio mini-meter widget
void DrawAudioMiniMeter(int xruns, float fill01);