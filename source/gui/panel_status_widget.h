// fw14.txt: Panel status widget for GUI diagnostics
#pragma once
#include "../core/panel_mp.h"
#include <imgui.h>
#include <string>

class PanelStatusWidget {
public:
    void render(PanelMP* panelMP) {
        if (!panelMP) return;
        
        auto status = panelMP->getStatus();
        
        ImGui::Begin("Panel-MP Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        // fw14.txt Source and mode display
        renderSourceStatus(status);
        ImGui::Separator();
        
        // Statistics
        renderStatistics(status);
        ImGui::Separator();
        
        // Last commands for debugging
        renderLastCommands(status);
        
        ImGui::End();
    }

private:
    void renderSourceStatus(const PanelMP::PanelStatus& status) {
        // Source display with color coding
        ImGui::Text("Source: ");
        ImGui::SameLine();
        
        ImVec4 sourceColor = getSourceColor(status.activeSource);
        ImGui::TextColored(sourceColor, "%s", getSourceName(status.activeSource).c_str());
        
        if (status.sourceLocked) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), " [LOCKED]");
        }
        
        // Mode and configuration
        ImGui::Text("Mode: %s", status.modeString.c_str());
        
        if (status.sciConfig.baud > 0) {
            ImGui::Text("SCI: %u %d%c%d", 
                status.sciConfig.baud, 
                status.sciConfig.dataBits,
                status.sciConfig.parity,
                status.sciConfig.stopBits);
        }
    }
    
    void renderStatistics(const PanelMP::PanelStatus& status) {
        ImGui::Text("Commands: %u", status.totalCommands);
        ImGui::Text("Data bytes: %u", status.totalDataBytes);
        
        // Last activity timing
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - status.lastActivity).count();
        
        if (ms < 1000) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Last activity: %lld ms ago", ms);
        } else {
            ImGui::Text("Last activity: %lld ms ago", ms);
        }
    }
    
    void renderLastCommands(const PanelMP::PanelStatus& status) {
        ImGui::Text("Last Commands:");
        if (status.lastCommands.empty()) {
            ImGui::Text("  (none)");
        } else {
            std::string cmdStr;
            for (size_t i = 0; i < status.lastCommands.size(); i++) {
                if (i > 0) cmdStr += " ";
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X", status.lastCommands[i]);
                cmdStr += hex;
            }
            ImGui::Text("  %s", cmdStr.c_str());
        }
    }
    
    std::string getSourceName(Source src) {
        switch (src) {
            case Source::SCI0: return "SCI0";
            case Source::SCI1: return "SCI1"; 
            case Source::SCI2: return "SCI2";
            case Source::HPI:  return "HPI";
            case Source::Test: return "TEST";
            default: return "Unknown";
        }
    }
    
    ImVec4 getSourceColor(Source src) {
        switch (src) {
            case Source::SCI0: return ImVec4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan
            case Source::SCI1: return ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
            case Source::SCI2: return ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
            case Source::HPI:  return ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta
            case Source::Test: return ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
            default: return ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red for unknown
        }
    }
};