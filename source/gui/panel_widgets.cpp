// fw28.txt: Panel control widgets for MS2000 Emulator GUI
// GPT5 Testvér panel widget implementation - 35 VR + Switches/LEDs

#include "imgui.h"
#include <array>
#include <string>
#include <functional>
#include <cstdint>

// Panel control widgets - VR/LED/SW grid mapped to MS2000 hardware
void DrawPanelControls(
    const std::function<void(uint8_t, bool)>& sendSwitch,
    const std::function<void(uint8_t, bool)>& sendLed,
    const std::function<void(uint8_t, float)>& sendVr)
{
    ImGui::PushID("PanelControls");

    // --- Virtual Rotaries (35 potentiometers) - 3 columns, 0..1 normalized sliders ---
    static std::array<float, 35> vrVal{};
    static const char* vrNames[35] = {
        "OSC1 Pitch",    "OSC1 Wave",     "OSC1 Mod",      "OSC2 Pitch",    "OSC2 Detune",
        "OSC Mix",       "Noise Level",   "Filter Cutoff", "Filter Reso",   "Filter EG Int",
        "Filter KeyTrk", "Amp Level",     "Porta Time",    "Patch Depth",   "LFO1 Rate",
        "LFO1 Int",      "LFO2 Rate",     "LFO2 Int",      "EG1 Attack",    "EG1 Decay",
        "EG1 Sustain",   "EG1 Release",   "EG2 Attack",    "EG2 Decay",     "EG2 Sustain",
        "EG2 Release",   "Delay Time",    "Delay Depth",   "FX Param1",     "FX Param2",
        "Drive Level",   "Pan Position",  "Arp Gate",      "Arp Tempo",     "Vocoder Mix"
    };

    ImGui::TextUnformatted("Virtual Rotaries (VR) - MS2000 Panel Emulation");
    ImGui::Separator();
    
    int cols = 3;
    ImGui::Columns(cols, "vrcols", false);
    
    for (int i = 0; i < 35; i++) {
        ImGui::PushID(i);
        
        // VR label with ID
        ImGui::Text("VR%02d", i);
        ImGui::Text("%s", vrNames[i]);
        
        // Slider for VR control
        float v = vrVal[i];
        if (ImGui::SliderFloat("##vrslider", &v, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            // Send VR change to emulator core
            // Debounce/rate-limit handled at PanelMP level in fw23
            sendVr((uint8_t)i, v);
            vrVal[i] = v;
        }
        
        // Value display
        ImGui::Text("Val: %d", (int)(v * 1023.0f));
        
        ImGui::PopID();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    ImGui::Separator();

    // --- Switches & LEDs - MS2000 specific controls ---
    ImGui::TextUnformatted("Panel Switches & LEDs");
    
    // Main function switches
    static bool swExit = false, swWrite = false, swPage = false;
    static bool swEdit = false, swPatch = false, swProgram = false;
    
    ImGui::Text("Function Switches:");
    if (ImGui::Checkbox("EXIT", &swExit)) sendSwitch(0, swExit);
    ImGui::SameLine();
    if (ImGui::Checkbox("WRITE", &swWrite)) sendSwitch(1, swWrite);
    ImGui::SameLine();
    if (ImGui::Checkbox("PAGE", &swPage)) sendSwitch(2, swPage);
    
    if (ImGui::Checkbox("EDIT", &swEdit)) sendSwitch(3, swEdit);
    ImGui::SameLine();
    if (ImGui::Checkbox("PATCH", &swPatch)) sendSwitch(4, swPatch);
    ImGui::SameLine();
    if (ImGui::Checkbox("PROGRAM", &swProgram)) sendSwitch(5, swProgram);

    ImGui::Separator();

    // Oscillator LEDs
    static bool ledOsc1Saw = false, ledOsc1Squ = false, ledOsc1Tri = false;
    static bool ledOsc2Saw = false, ledOsc2Squ = false, ledOsc2Tri = false;
    
    ImGui::Text("OSC1 Waveform LEDs:");
    if (ImGui::Checkbox("OSC1 SAW", &ledOsc1Saw)) sendLed(0, ledOsc1Saw);
    ImGui::SameLine();
    if (ImGui::Checkbox("OSC1 SQU", &ledOsc1Squ)) sendLed(1, ledOsc1Squ);
    ImGui::SameLine();
    if (ImGui::Checkbox("OSC1 TRI", &ledOsc1Tri)) sendLed(2, ledOsc1Tri);
    
    ImGui::Text("OSC2 Waveform LEDs:");
    if (ImGui::Checkbox("OSC2 SAW", &ledOsc2Saw)) sendLed(3, ledOsc2Saw);
    ImGui::SameLine();
    if (ImGui::Checkbox("OSC2 SQU", &ledOsc2Squ)) sendLed(4, ledOsc2Squ);
    ImGui::SameLine();
    if (ImGui::Checkbox("OSC2 TRI", &ledOsc2Tri)) sendLed(5, ledOsc2Tri);

    ImGui::Separator();

    // Filter LEDs  
    static bool ledFilterLPF = false, ledFilterBPF = false, ledFilterHPF = false;
    
    ImGui::Text("Filter Type LEDs:");
    if (ImGui::Checkbox("LPF", &ledFilterLPF)) sendLed(6, ledFilterLPF);
    ImGui::SameLine();
    if (ImGui::Checkbox("BPF", &ledFilterBPF)) sendLed(7, ledFilterBPF);
    ImGui::SameLine();
    if (ImGui::Checkbox("HPF", &ledFilterHPF)) sendLed(8, ledFilterHPF);

    ImGui::Separator();

    // LFO LEDs
    static bool ledLfo1Saw = false, ledLfo1Squ = false, ledLfo1Tri = false, ledLfo1SH = false;
    
    ImGui::Text("LFO1 Waveform LEDs:");
    if (ImGui::Checkbox("LFO1 SAW", &ledLfo1Saw)) sendLed(9, ledLfo1Saw);
    ImGui::SameLine();
    if (ImGui::Checkbox("LFO1 SQU", &ledLfo1Squ)) sendLed(10, ledLfo1Squ);
    ImGui::SameLine();
    if (ImGui::Checkbox("LFO1 TRI", &ledLfo1Tri)) sendLed(11, ledLfo1Tri);
    ImGui::SameLine();
    if (ImGui::Checkbox("LFO1 S&H", &ledLfo1SH)) sendLed(12, ledLfo1SH);

    ImGui::Separator();

    // Arpeggiator controls
    static bool swArpOn = false, swArpHold = false;
    static bool ledArpUp = false, ledArpDown = false, ledArpUpDown = false, ledArpRandom = false;
    
    ImGui::Text("Arpeggiator:");
    if (ImGui::Checkbox("ARP ON", &swArpOn)) sendSwitch(6, swArpOn);
    ImGui::SameLine();
    if (ImGui::Checkbox("ARP HOLD", &swArpHold)) sendSwitch(7, swArpHold);
    
    ImGui::Text("Arp Pattern LEDs:");
    if (ImGui::Checkbox("UP", &ledArpUp)) sendLed(13, ledArpUp);
    ImGui::SameLine();
    if (ImGui::Checkbox("DOWN", &ledArpDown)) sendLed(14, ledArpDown);
    ImGui::SameLine();
    if (ImGui::Checkbox("UP/DOWN", &ledArpUpDown)) sendLed(15, ledArpUpDown);
    ImGui::SameLine();
    if (ImGui::Checkbox("RANDOM", &ledArpRandom)) sendLed(16, ledArpRandom);

    ImGui::PopID();
}

// Audio mini-meter widget - shows ring buffer fill and xruns
void DrawAudioMiniMeter(int xruns, float fill01)
{
    ImGui::PushID("AudioMeter");
    
    // Audio status text
    ImGui::Text("Audio Status: fill=%.1f%% xruns=%d", fill01 * 100.0f, xruns);
    
    // Progress bar for buffer fill
    ImVec4 barColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
    if (fill01 > 0.8f) {
        barColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow if high
    }
    if (fill01 > 0.9f) {
        barColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red if very high
    }
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
    ImGui::ProgressBar(fill01, ImVec2(200, 12), "");
    ImGui::PopStyleColor();
    
    // XRUN indicator
    if (xruns > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "XRUN!");
    }
    
    // Additional audio info
    ImGui::Text("fw19.txt: I²S/AK4522 Audio Pipeline");
    ImGui::Text("Sample Rate: 48kHz | Buffer: 256 frames");
    
    ImGui::PopID();
}