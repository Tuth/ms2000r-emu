// fw14.txt: Bridge between MS2000 runner and LCD diagnostics widget
#pragma once
#include "../core/ms2000_runner.h"
#include "../core/lcd_boot_probe.h"
#include "lcd_diagnostics_widget.h"

class DiagnosticsBridge {
public:
    static void updateDiagnosticsFromRunner(LcdDiagnosticsWidget& widget, MS2000::Ms2kRunner* runner) {
        if (!runner) return;
        
        try {
            // Update boot probe data
            const auto& probe = runner->getLcdBootProbe();
            widget.updateBootProbe(
                probe.got_fn,      // Function Set
                probe.got_on,      // Display ON  
                probe.got_clr,     // Clear Display
                probe.got_ent,     // Entry Mode
                probe.got_ddram0,  // DDRAM Address Set
                probe.data_chars   // Data characters written
            );
            
            // Update system gates
            widget.updateSystemGates(
                probe.g_dsp_ack,       // DSP ACK received
                probe.g_codec_unmuted, // CODEC initialized
                probe.g_panel_ok,      // Panel interface OK
                probe.g_tick_ok,       // CPU tick active
                probe.g_vbr_ok         // Vector Base Register OK
            );
            
            // Update command tracking from PanelMP if available
            auto panelMP = runner->getPanelMP();
            if (panelMP) {
                auto status = panelMP->getStatus();
                
                // Track recent commands from PanelMP's last commands buffer
                for (uint8_t cmd : status.lastCommands) {
                    if (cmd != 0) {  // Skip empty slots
                        widget.trackCommand(cmd);
                    }
                }
            }
            
        } catch (const std::exception& e) {
            // Silently handle exceptions to avoid GUI crashes
            // Could log error if logging system is available
        }
    }
    
    // Helper to extract system health information
    static int calculateSystemHealth(const LcdBootProbe& probe) {
        int score = 0;
        if (probe.g_dsp_ack) score += 20;
        if (probe.g_codec_unmuted) score += 20;
        if (probe.g_panel_ok) score += 20;
        if (probe.g_tick_ok) score += 20;
        if (probe.g_vbr_ok) score += 20;
        return score;
    }
    
    // Helper to calculate boot completion percentage
    static int calculateBootCompletion(const LcdBootProbe& probe) {
        int steps = 0;
        if (probe.got_fn) steps++;
        if (probe.got_on) steps++;
        if (probe.got_clr) steps++;
        if (probe.got_ent) steps++;
        if (probe.got_ddram0) steps++;
        return (steps * 100) / 5;
    }
    
    // Helper to format elapsed time since boot
    static std::string formatBootTime(const LcdBootProbe& probe) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - probe.t0);
        
        if (elapsed.count() < 1000) {
            return std::to_string(elapsed.count()) + "ms";
        } else {
            return std::to_string(elapsed.count() / 1000) + "s";
        }
    }
    
    // Helper to check if firmware is actively running
    static bool isFirmwareActive(const LcdBootProbe& probe) {
        auto now = std::chrono::steady_clock::now();
        auto lastActivity = std::max(probe.last_cmd, probe.last_data);
        auto silence = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivity);
        
        // Consider active if there was activity in the last 5 seconds
        return silence.count() < 5000;
    }
};