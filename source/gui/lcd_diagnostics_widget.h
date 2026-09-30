// fw14.txt: LCD diagnostics overlay with boot probe status and command counters
#pragma once
#include "../core/lcd_gui.h"
#include "../core/panel_mp.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <chrono>

class LcdDiagnosticsWidget {
public:
    LcdDiagnosticsWidget() {
        // Initialize diagnostic state
        reset();
    }
    
    void render(const LcdGuiSnapshot& snapshot, PanelMP* panelMP = nullptr) {
        ImGui::Begin("LCD Diagnostics Overlay", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        renderBootProbe();
        ImGui::Separator();
        renderCommandCounters(panelMP);
        ImGui::Separator();
        renderLcdState(snapshot);
        ImGui::Separator();
        renderSystemGates();
        ImGui::Separator();
        renderControls();

        ImGui::End();
    }
    
    // Update diagnostic data
    void updateBootProbe(bool fn, bool on, bool clr, bool ent, bool ddram, int dataCount) {
        m_bootProbe.functionSet = fn;
        m_bootProbe.displayOn = on;
        m_bootProbe.clearDisplay = clr;
        m_bootProbe.entryMode = ent;
        m_bootProbe.ddramSet = ddram;
        m_bootProbe.dataCount = dataCount;
        m_bootProbe.lastUpdate = std::chrono::steady_clock::now();
        
        // Calculate boot completion percentage
        int completed = (fn ? 1 : 0) + (on ? 1 : 0) + (clr ? 1 : 0) + (ent ? 1 : 0) + (ddram ? 1 : 0);
        m_bootProbe.completionPercent = (completed * 100) / 5;
    }
    
    void updateSystemGates(bool dsp, bool codec, bool panel, bool tick, bool vbr) {
        m_systemGates.dsp = dsp;
        m_systemGates.codec = codec;
        m_systemGates.panel = panel;
        m_systemGates.tick = tick;
        m_systemGates.vbr = vbr;
        
        int activeGates = (dsp ? 1 : 0) + (codec ? 1 : 0) + (panel ? 1 : 0) + (tick ? 1 : 0) + (vbr ? 1 : 0);
        m_systemGates.healthPercent = (activeGates * 100) / 5;
    }
    
    void trackCommand(uint8_t cmd) {
        m_commandStats.totalCommands++;
        m_recentCommands.push_back({cmd, std::chrono::steady_clock::now()});
        
        // Keep only last 16 commands
        if (m_recentCommands.size() > 16) {
            m_recentCommands.erase(m_recentCommands.begin());
        }
        
        // Track command types
        if (cmd == 0x01) m_commandStats.clearCount++;
        else if (cmd == 0x38) m_commandStats.functionSetCount++;
        else if ((cmd & 0xF8) == 0x08) m_commandStats.displayControlCount++;
        else if ((cmd & 0x80) == 0x80) m_commandStats.setDdramCount++;
        else if ((cmd & 0x40) == 0x40) m_commandStats.setCgramCount++;
    }
    
    void trackData(uint8_t data) {
        m_commandStats.totalData++;
        m_commandStats.lastDataTime = std::chrono::steady_clock::now();
    }
    
    void reset() {
        m_bootProbe = {};
        m_systemGates = {};
        m_commandStats = {};
        m_recentCommands.clear();
        m_overlayEnabled = true;
    }

private:
    struct BootProbeData {
        bool functionSet = false;    // FN (0x38)
        bool displayOn = false;      // ON (0x0C)
        bool clearDisplay = false;   // CLR (0x01)
        bool entryMode = false;      // ENT (0x06)
        bool ddramSet = false;       // DDRAM (0x80+)
        int dataCount = 0;           // DATA count
        int completionPercent = 0;
        std::chrono::steady_clock::time_point lastUpdate;
    };
    
    struct SystemGatesData {
        bool dsp = false;
        bool codec = false;
        bool panel = false;
        bool tick = false;
        bool vbr = false;
        int healthPercent = 0;
    };
    
    struct CommandStats {
        uint32_t totalCommands = 0;
        uint32_t totalData = 0;
        uint32_t clearCount = 0;
        uint32_t functionSetCount = 0;
        uint32_t displayControlCount = 0;
        uint32_t setDdramCount = 0;
        uint32_t setCgramCount = 0;
        std::chrono::steady_clock::time_point lastDataTime;
    };
    
    struct RecentCommand {
        uint8_t cmd;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    BootProbeData m_bootProbe;
    SystemGatesData m_systemGates;
    CommandStats m_commandStats;
    std::vector<RecentCommand> m_recentCommands;
    bool m_overlayEnabled = true;
    
    void renderBootProbe() {
        ImGui::Text("Boot Probe Status");

        // Progress bar for boot completion
        ImVec4 progressColor = m_bootProbe.completionPercent == 100 ?
            ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, progressColor);
        ImGui::ProgressBar(m_bootProbe.completionPercent / 100.0f, ImVec2(-1, 0),
                          ("Boot: " + std::to_string(m_bootProbe.completionPercent) + "%").c_str());
        ImGui::PopStyleColor();

        // Boot sequence checklist
        ImGui::Text("LCD Init Sequence:");
        renderStatusFlag("FN", m_bootProbe.functionSet, "Function Set (0x38)");
        ImGui::SameLine();
        renderStatusFlag("ON", m_bootProbe.displayOn, "Display ON (0x0C)");
        ImGui::SameLine();
        renderStatusFlag("CLR", m_bootProbe.clearDisplay, "Clear Display (0x01)");
        ImGui::SameLine();
        renderStatusFlag("ENT", m_bootProbe.entryMode, "Entry Mode (0x06)");
        ImGui::SameLine();
        renderStatusFlag("DDRAM", m_bootProbe.ddramSet, "DDRAM Address Set");

        ImGui::Text("DATA: %d characters written", m_bootProbe.dataCount);

        // Time since last update
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_bootProbe.lastUpdate).count();
        ImGui::Text("Last update: %lld ms ago", elapsed);
    }
    
    void renderStatusFlag(const char* name, bool status, const char* tooltip) {
        ImVec4 color = status ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(color, "%s", name);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
    }
    
    void renderCommandCounters(PanelMP* panelMP) {
        ImGui::Text("Command Counters");

        // Get stats from PanelMP if available
        uint32_t totalCmds = m_commandStats.totalCommands;
        uint32_t totalData = m_commandStats.totalData;

        if (panelMP) {
            auto status = panelMP->getStatus();
            totalCmds = status.totalCommands;
            totalData = status.totalDataBytes;
        }

        ImGui::Text("Total Commands: %u", totalCmds);
        ImGui::SameLine();
        ImGui::Text("Total Data: %u", totalData);

        // Command type breakdown
        ImGui::Text("Command Types:");
        ImGui::Text("  Clear (0x01): %u", m_commandStats.clearCount);
        ImGui::Text("  Function Set (0x38): %u", m_commandStats.functionSetCount);
        ImGui::Text("  Display Control (0x08-0x0F): %u", m_commandStats.displayControlCount);
        ImGui::Text("  Set DDRAM (0x80+): %u", m_commandStats.setDdramCount);
        ImGui::Text("  Set CGRAM (0x40+): %u", m_commandStats.setCgramCount);

        // Commands per second
        if (totalCmds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_commandStats.lastDataTime).count();
            if (elapsed > 0) {
                double cps = static_cast<double>(totalCmds) / elapsed;
                ImGui::Text("Commands/sec: %.2f", cps);
            }
        }
    }
    
    void renderLcdState(const LcdGuiSnapshot& snapshot) {
        ImGui::Text("LCD State");
        
        // Display flags
        ImGui::Text("Display: %s", snapshot.displayOn ? "ON" : "OFF");
        ImGui::SameLine();
        ImGui::Text("Cursor: %s", snapshot.cursorOn ? "ON" : "OFF");
        ImGui::SameLine();
        ImGui::Text("Blink: %s", snapshot.blinkOn ? "ON" : "OFF");
        
        // Cursor position
        ImGui::Text("Cursor Position: Line %d, Col %d", snapshot.curLine, snapshot.curCol);
        
        // Content analysis
        std::string line0(snapshot.line0);
        std::string line1(snapshot.line1);
        int totalChars = 0;
        int customChars = 0;
        
        for (char c : line0) {
            if (c != ' ') totalChars++;
            if (c >= 0 && c < 8) customChars++;
        }
        for (char c : line1) {
            if (c != ' ') totalChars++;
            if (c >= 0 && c < 8) customChars++;
        }
        
        ImGui::Text("Content: %d visible chars, %d custom chars", totalChars, customChars);
    }
    
    void renderSystemGates() {
        ImGui::Text("System Gates Status");
        
        // Health indicator
        ImVec4 healthColor = m_systemGates.healthPercent == 100 ? 
            ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : 
            (m_systemGates.healthPercent >= 60 ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, healthColor);
        ImGui::ProgressBar(m_systemGates.healthPercent / 100.0f, ImVec2(-1, 0),
                          ("System Health: " + std::to_string(m_systemGates.healthPercent) + "%").c_str());
        ImGui::PopStyleColor();
        
        // Individual gate status
        ImGui::Text("Gates:");
        renderStatusFlag("DSP", m_systemGates.dsp, "DSP communication active");
        ImGui::SameLine();
        renderStatusFlag("CODEC", m_systemGates.codec, "Audio codec initialized");
        ImGui::SameLine();
        renderStatusFlag("PANEL", m_systemGates.panel, "Panel interface active");
        ImGui::SameLine();
        renderStatusFlag("TICK", m_systemGates.tick, "CPU tick active");
        ImGui::SameLine();
        renderStatusFlag("VBR", m_systemGates.vbr, "Vector base register set");
    }
    
    void renderControls() {
        ImGui::Text("Controls");
        
        if (ImGui::Button("Reset Counters")) {
            reset();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Export Log")) {
            exportDiagnosticLog();
        }
        
        ImGui::Checkbox("Show Recent Commands", &m_showRecentCommands);
        if (m_showRecentCommands) {
            renderRecentCommands();
        }
    }
    
    void renderRecentCommands() {
        ImGui::Text("Recent Commands (last 16):");
        
        for (auto it = m_recentCommands.rbegin(); it != m_recentCommands.rend(); ++it) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - it->timestamp).count();
            
            ImGui::Text("%3lld ms: 0x%02X (%s)", elapsed, it->cmd, getCommandName(it->cmd).c_str());
        }
    }
    
    std::string getCommandName(uint8_t cmd) {
        if (cmd == 0x01) return "Clear Display";
        if (cmd == 0x02) return "Return Home";
        if (cmd == 0x38) return "Function Set";
        if ((cmd & 0xF8) == 0x08) return "Display Control";
        if ((cmd & 0xF0) == 0x00) return "Entry Mode Set";
        if ((cmd & 0x80) == 0x80) return "Set DDRAM";
        if ((cmd & 0x40) == 0x40) return "Set CGRAM";
        return "Unknown";
    }
    
    void exportDiagnosticLog() {
        // Simple diagnostic export (could be expanded to file I/O)
        std::string log = "LCD Diagnostics Export\n";
        log += "======================\n";
        log += "Boot Probe: " + std::to_string(m_bootProbe.completionPercent) + "% complete\n";
        log += "Total Commands: " + std::to_string(m_commandStats.totalCommands) + "\n";
        log += "Total Data: " + std::to_string(m_commandStats.totalData) + "\n";
        log += "System Health: " + std::to_string(m_systemGates.healthPercent) + "%\n";
        
        // Copy to clipboard or show in console (simplified)
        ImGui::LogToClipboard();
        ImGui::LogText("%s", log.c_str());
        ImGui::LogFinish();
        
        // Status message
        ImGui::OpenPopup("Export Complete");
    }
    
    bool m_showRecentCommands = false;
};