// fw27.txt Phase 5: Panel timeline & LCD diff visualization
// Visual debugging for complex UI flows with DDRAM tracking and frame timeline

#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace MS2000 {

// Single LCD state snapshot for diff comparison
struct LcdSnapshot {
    std::chrono::steady_clock::time_point timestamp;
    char line0[17] = {0};  // 16 chars + null terminator
    char line1[17] = {0};
    uint32_t ddramWriteCount = 0;  // DDRAM writes since boot
    uint32_t frameNumber = 0;      // Sequential frame number
    
    bool operator==(const LcdSnapshot& other) const {
        return std::string(line0) == std::string(other.line0) &&
               std::string(line1) == std::string(other.line1);
    }
    
    bool operator!=(const LcdSnapshot& other) const {
        return !(*this == other);
    }
};

// PanelMP frame for timeline visualization
struct PanelFrame {
    std::chrono::steady_clock::time_point timestamp;
    uint8_t opCode;
    std::vector<uint8_t> payload;
    std::string direction;  // "IN" or "OUT"
    uint32_t frameNumber;
    std::string description;  // Human-readable description
    
    // Generate description from opcode and payload
    void generateDescription() {
        switch (opCode) {
            case 0x01: description = "LED_SET"; break;
            case 0x02: description = "VR_READ"; break;
            case 0x03: description = "SW_READ"; break;
            case 0x10: description = "LCD_DDRAM_WRITE"; break;
            case 0x11: description = "LCD_CGRAM_WRITE"; break;
            case 0x12: description = "LCD_CONTROL"; break;
            default: 
                description = "UNKNOWN_0x";
                if (opCode < 16) description += "0";
                description += std::to_string(opCode);
                break;
        }
        
        if (!payload.empty()) {
            description += " [" + std::to_string(payload.size()) + " bytes]";
        }
    }
};

// LCD difference record for visualization
struct LcdDiff {
    std::chrono::steady_clock::time_point timestamp;
    uint32_t frameNumber;
    std::string line0Before;
    std::string line0After;
    std::string line1Before;
    std::string line1After;
    std::vector<uint8_t> changedPositions;  // Positions that changed
    std::string changeDescription;
    
    bool hasChanges() const {
        return !changedPositions.empty();
    }
};

// Main visualization system
class PanelTimelineVisualizer {
public:
    PanelTimelineVisualizer() {
        m_startTime = std::chrono::steady_clock::now();
        clear();
    }
    
    // Clear all tracked data
    void clear() {
        m_lcdHistory.clear();
        m_panelFrames.clear();
        m_lcdDiffs.clear();
        m_frameCounter = 0;
        m_ddramWriteCounter = 0;
    }
    
    // Track LCD state changes
    void trackLcdState(const char* line0, const char* line1) {
        LcdSnapshot snapshot;
        snapshot.timestamp = std::chrono::steady_clock::now();
        snapshot.frameNumber = ++m_frameCounter;
        snapshot.ddramWriteCount = m_ddramWriteCounter;
        
        // Safe string copy
        std::memcpy(snapshot.line0, line0 ? line0 : "", std::min<size_t>(16, std::strlen(line0 ? line0 : "")));
        std::memcpy(snapshot.line1, line1 ? line1 : "", std::min<size_t>(16, std::strlen(line1 ? line1 : "")));
        snapshot.line0[16] = '\0';
        snapshot.line1[16] = '\0';
        
        // Check for changes and create diff
        if (!m_lcdHistory.empty()) {
            const auto& lastSnapshot = m_lcdHistory.back();
            if (snapshot != lastSnapshot) {
                LcdDiff diff;
                diff.timestamp = snapshot.timestamp;
                diff.frameNumber = snapshot.frameNumber;
                diff.line0Before = lastSnapshot.line0;
                diff.line0After = snapshot.line0;
                diff.line1Before = lastSnapshot.line1;
                diff.line1After = snapshot.line1;
                
                // Find changed positions
                findChangedPositions(diff);
                generateChangeDescription(diff);
                
                m_lcdDiffs.push_back(diff);
            }
        }
        
        m_lcdHistory.push_back(snapshot);
        
        // Keep history manageable (last 1000 snapshots)
        if (m_lcdHistory.size() > 1000) {
            m_lcdHistory.erase(m_lcdHistory.begin());
        }
    }
    
    // Track PanelMP frame
    void trackPanelFrame(uint8_t opCode, const std::vector<uint8_t>& payload, 
                        const std::string& direction) {
        PanelFrame frame;
        frame.timestamp = std::chrono::steady_clock::now();
        frame.opCode = opCode;
        frame.payload = payload;
        frame.direction = direction;
        frame.frameNumber = ++m_frameCounter;
        frame.generateDescription();
        
        // Increment DDRAM counter for LCD writes
        if (opCode == 0x10) {  // LCD_DDRAM_WRITE
            m_ddramWriteCounter++;
        }
        
        m_panelFrames.push_back(frame);
        
        // Keep frame history manageable (last 5000 frames)
        if (m_panelFrames.size() > 5000) {
            m_panelFrames.erase(m_panelFrames.begin());
        }
    }
    
    // Get current LCD diff overlay information
    std::string getLcdDiffOverlay() const {
        if (m_lcdDiffs.empty()) {
            return "LCD Diff: No changes detected";
        }
        
        const auto& lastDiff = m_lcdDiffs.back();
        auto timeSinceLastChange = std::chrono::steady_clock::now() - lastDiff.timestamp;
        auto millisSinceChange = std::chrono::duration_cast<std::chrono::milliseconds>(timeSinceLastChange).count();
        
        std::stringstream overlay;
        overlay << "LCD Diff (Frame #" << lastDiff.frameNumber << ", " << millisSinceChange << "ms ago):\n";
        overlay << "  Changes: " << lastDiff.changeDescription << "\n";
        overlay << "  DDRAM writes: " << m_ddramWriteCounter;
        
        return overlay.str();
    }
    
    // Export timeline visualization to PNG-compatible text format
    bool exportTimelinePNG(const std::string& filename) const {
        std::ofstream file(filename + ".txt");  // Text format for now
        if (!file) return false;
        
        file << "MS2000 Panel Timeline Visualization\n";
        file << "====================================\n";
        file << "Generated: " << getCurrentTimestamp() << "\n";
        file << "Total Frames: " << m_frameCounter << "\n";
        file << "DDRAM Writes: " << m_ddramWriteCounter << "\n";
        file << "LCD Changes: " << m_lcdDiffs.size() << "\n\n";
        
        // Panel frame timeline
        file << "PANEL FRAME TIMELINE:\n";
        file << "---------------------\n";
        for (const auto& frame : m_panelFrames) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                frame.timestamp - m_startTime).count();
            
            file << "[" << std::setw(6) << elapsed << "ms] "
                 << std::setw(3) << frame.direction << " "
                 << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)frame.opCode
                 << std::dec << std::setfill(' ') << " "
                 << std::setw(15) << frame.description
                 << " (#" << frame.frameNumber << ")\n";
        }
        
        file << "\nLCD CHANGE TIMELINE:\n";
        file << "--------------------\n";
        for (const auto& diff : m_lcdDiffs) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                diff.timestamp - m_startTime).count();
                
            file << "[" << std::setw(6) << elapsed << "ms] "
                 << "Frame #" << std::setw(4) << diff.frameNumber << " "
                 << diff.changeDescription << "\n";
            file << "  L0: [" << diff.line0Before << "] -> [" << diff.line0After << "]\n";
            file << "  L1: [" << diff.line1Before << "] -> [" << diff.line1After << "]\n\n";
        }
        
        return true;
    }
    
    // Export visual switch/VR sequence for debugging
    bool exportSwitchSequence(const std::string& filename, 
                             std::chrono::milliseconds timeWindow) const {
        std::ofstream file(filename + "_sequence.txt");
        if (!file) return false;
        
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - timeWindow;
        
        file << "MS2000 Switch/VR Sequence Analysis\n";
        file << "==================================\n";
        file << "Time Window: Last " << timeWindow.count() << "ms\n\n";
        
        // Filter recent frames for switch/VR activity
        std::vector<PanelFrame> recentFrames;
        std::copy_if(m_panelFrames.begin(), m_panelFrames.end(),
                    std::back_inserter(recentFrames),
                    [cutoff](const PanelFrame& frame) {
                        return frame.timestamp >= cutoff &&
                               (frame.opCode == 0x02 || frame.opCode == 0x03); // VR_READ or SW_READ
                    });
        
        file << "RECENT SWITCH/VR ACTIVITY:\n";
        file << "---------------------------\n";
        for (const auto& frame : recentFrames) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                frame.timestamp - m_startTime).count();
            
            file << "[" << std::setw(6) << elapsed << "ms] "
                 << frame.description;
            
            if (!frame.payload.empty()) {
                file << " Data: ";
                for (uint8_t byte : frame.payload) {
                    file << "0x" << std::hex << std::setw(2) << std::setfill('0') 
                         << (int)byte << " ";
                }
            }
            file << "\n";
        }
        
        return true;
    }
    
    // Get statistics for debugging
    struct Statistics {
        uint32_t totalFrames = 0;
        uint32_t totalDdramWrites = 0;
        uint32_t lcdChanges = 0;
        uint32_t recentFrames = 0;  // Last 5 seconds
        std::chrono::milliseconds uptime{0};
    };
    
    Statistics getStatistics() const {
        Statistics stats;
        stats.totalFrames = m_frameCounter;
        stats.totalDdramWrites = m_ddramWriteCounter;
        stats.lcdChanges = m_lcdDiffs.size();
        stats.uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_startTime);
        
        // Count recent frames (last 5 seconds)
        auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        stats.recentFrames = std::count_if(m_panelFrames.begin(), m_panelFrames.end(),
            [cutoff](const PanelFrame& frame) { return frame.timestamp >= cutoff; });
        
        return stats;
    }

private:
    std::chrono::steady_clock::time_point m_startTime;
    std::vector<LcdSnapshot> m_lcdHistory;
    std::vector<PanelFrame> m_panelFrames;
    std::vector<LcdDiff> m_lcdDiffs;
    uint32_t m_frameCounter = 0;
    uint32_t m_ddramWriteCounter = 0;
    
    void findChangedPositions(LcdDiff& diff) const {
        diff.changedPositions.clear();
        
        // Check line 0
        for (int i = 0; i < 16 && i < (int)diff.line0Before.size() && i < (int)diff.line0After.size(); ++i) {
            if (diff.line0Before[i] != diff.line0After[i]) {
                diff.changedPositions.push_back(i);
            }
        }
        
        // Check line 1 (add 16 for line 1 positions)
        for (int i = 0; i < 16 && i < (int)diff.line1Before.size() && i < (int)diff.line1After.size(); ++i) {
            if (diff.line1Before[i] != diff.line1After[i]) {
                diff.changedPositions.push_back(16 + i);
            }
        }
    }
    
    void generateChangeDescription(LcdDiff& diff) const {
        if (diff.changedPositions.empty()) {
            diff.changeDescription = "No changes detected";
            return;
        }
        
        std::stringstream desc;
        desc << diff.changedPositions.size() << " char";
        if (diff.changedPositions.size() != 1) desc << "s";
        desc << " changed";
        
        // Add position details
        if (diff.changedPositions.size() <= 4) {
            desc << " at pos ";
            for (size_t i = 0; i < diff.changedPositions.size(); ++i) {
                if (i > 0) desc << ",";
                desc << static_cast<int>(diff.changedPositions[i]);
            }
        }
        
        diff.changeDescription = desc.str();
    }
    
    std::string getCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        struct tm time_info;
        localtime_s(&time_info, &time_t);
        ss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
};

} // namespace MS2000