// fw17.txt: ImGui diagnostic panel widget for MS2000 system monitoring
// Real-time developer radar showing source, baud, parity, HPI order, LCD mode
#pragma once
#include "../core/diag_panel.h"
#include <string>

// Helper functions for diagnostic display
inline const char* srcName(PanelSrc s) {
    switch(s) { 
        case PanelSrc::SCI0: return "SCI0";
        case PanelSrc::SCI1: return "SCI1";
        case PanelSrc::SCI2: return "SCI2";
        case PanelSrc::HPI: return "HPI";
        default: return "Unknown";
    }
}

inline const char* modeName(PanelMode m) {
    switch(m) { 
        case PanelMode::Probe: return "Probe";
        case PanelMode::LenPref: return "LenPref";
        case PanelMode::Header: return "Header";
        case PanelMode::RawLCD: return "RawLCD";
    }
    return "?";
}

inline const char* hpiName(HpiOrder o) {
    switch(o) { 
        case HpiOrder::Auto: return "Auto";
        case HpiOrder::HiLo: return "HiLo";
        case HpiOrder::LoHi: return "LoHi";
    }
    return "?";
}

class DiagPanelWidget {
public:
    void render(const DiagSnapshot& snapshot, bool* pOpen = nullptr) {
        // fw17.txt: MS2000 diagnostic panel with developer radar
        if (!ImGui::Begin("MS2000 Diagnostics", pOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }
        
        // fw17.txt: Main status line - source, mode, HPI order
        ImGui::TextColored(getSourceColor(snapshot.src), "Panel Source: %s", srcName(snapshot.src));
        ImGui::SameLine(); 
        ImGui::TextDisabled(" Mode: %s", modeName(snapshot.mode));
        ImGui::SameLine(); 
        ImGui::TextDisabled(" HPI Order: %s", hpiName(snapshot.hpiOrder));
        ImGui::Separator();
        
        // fw17.txt: Recording/Replay status
        if (snapshot.recording || snapshot.replaying) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Recording: %s | Replaying: %s", 
                snapshot.recording ? "ON" : "OFF", snapshot.replaying ? "ON" : "OFF");
            if (snapshot.deterministicSeed != 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(" Seed: %llu", (unsigned long long)snapshot.deterministicSeed);
            }
            ImGui::Separator();
        }
        
        // fw17.txt: PanelMP statistics
        ImGui::Text("PanelMP: bytes=%llu frames=%llu", 
                   (unsigned long long)snapshot.mpBytes, 
                   (unsigned long long)snapshot.mpFrames);
        
        // fw17.txt: SCI channels status with collapsible header
        if (ImGui::CollapsingHeader("SCI Channels", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < 3; i++) {
                const auto& d = snapshot.sci[i];
                if (d.present) {
                    ImGui::Text("SCI%d: present  baud=%u  %d%c%d  TE=%d  CM=%d  TX=%llu",
                               i, d.baud, d.dataBits, d.parity, d.stopBits,
                               d.txEnabled ? 1 : 0, d.syncMode ? 1 : 0, 
                               (unsigned long long)d.txBytes);
                } else {
                    ImGui::TextDisabled("SCI%d: not present", i);
                }
            }
        }
        
        // fw17.txt: LCD Boot Probe diagnostics
        if (ImGui::CollapsingHeader("LCD Boot Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("FN=%d ON=%d CLR=%d ENT=%d DDRAM=%d DATA=%u",
                       snapshot.boot.fn ? 1 : 0, snapshot.boot.on ? 1 : 0, 
                       snapshot.boot.clr ? 1 : 0, snapshot.boot.ent ? 1 : 0, 
                       snapshot.boot.ddram ? 1 : 0, snapshot.boot.dataCount);
            
            // Visual progress indicator for boot sequence
            float progress = calculateBootProgress(snapshot.boot);
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), 
                              progress >= 1.0f ? "Boot Complete" : "Boot In Progress");
        }
        
        // fw17.txt: Deterministic timing info
        if (snapshot.tickCount > 0) {
            ImGui::Separator();
            ImGui::Text("Deterministic: tick=%llu (%.3f sec)", 
                       (unsigned long long)snapshot.tickCount,
                       snapshot.tickCount / 1000.0);
        }
        
        // fw20.txt: Async log diagnostics
        if (snapshot.droppedLogs > 0) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), 
                              "Dropped Logs: %llu", (unsigned long long)snapshot.droppedLogs);
        }
        
        // fw17.txt: Notes section (free text)
        if (!snapshot.notes.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Notes: %s", snapshot.notes.c_str());
        }
        
        ImGui::End();
    }
    
private:
    // Color coding for panel source status
    ImVec4 getSourceColor(PanelSrc src) {
        switch(src) {
            case PanelSrc::SCI0: return ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green
            case PanelSrc::SCI1: return ImVec4(0.0f, 0.8f, 1.0f, 1.0f);  // Cyan  
            case PanelSrc::SCI2: return ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow
            case PanelSrc::HPI:  return ImVec4(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta
            default: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);              // Gray
        }
    }
    
    // Calculate LCD boot sequence progress
    float calculateBootProgress(const BootProbeDiag& boot) {
        int completed = 0;
        if (boot.fn) completed++;
        if (boot.on) completed++;
        if (boot.clr) completed++;
        if (boot.ent) completed++;
        if (boot.ddram) completed++;
        return completed / 5.0f;
    }
};

// Include ImGui here since this is the widget header
#ifdef IMGUI_VERSION
// Forward declare ImGui functions used above
#else
// Stub implementations for when ImGui is not available
namespace ImGui {
    inline bool Begin(const char*, bool*, int) { return false; }
    inline void End() {}
    inline void Text(const char*, ...) {}
    inline void TextColored(ImVec4, const char*, ...) {}
    inline void TextDisabled(const char*, ...) {}
    inline void TextWrapped(const char*, ...) {}
    inline void SameLine() {}
    inline void Separator() {}
    inline bool CollapsingHeader(const char*, int) { return false; }
    inline void ProgressBar(float, ImVec2, const char*) {}
}
struct ImVec4 { float x, y, z, w; ImVec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {} };
enum ImGuiWindowFlags_ { ImGuiWindowFlags_AlwaysAutoResize = 0 };
enum ImGuiTreeNodeFlags_ { ImGuiTreeNodeFlags_DefaultOpen = 0 };
struct ImVec2 { float x, y; ImVec2(float x, float y) : x(x), y(y) {} };
#endif