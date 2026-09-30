#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace MS2000 {

class H8S2350Emulator;

// Configuration for vector table tracking (from readme.nfo spec)
struct VecTrackCfg {
    uint32_t ramLo = 0x100000;  // MS2000 RAM start (per memory map)
    uint32_t ramHi = 0x17FFFF;  // MS2000 RAM end 
    uint16_t baseCandidates[5] = {0x00, 0x10, 0x20, 0x40, 0x80};  // TRAPA base candidates
    int minValid = 16;          // Minimum valid vectors needed to confirm table
};

/**
 * VectorTableTracker - Runtime RAM Vector Table Detection System
 * 
 * Purpose: Detect when firmware copies vector table to RAM and automatically
 * switch from TRAPA emulation mode to native vector mode.
 * 
 * Implementation follows readme.nfo specification for dynamic vector detection.
 */
class VectorTableTracker {
public:
    VectorTableTracker(H8S2350Emulator& cpu, const VecTrackCfg& cfg);
    
    // RAM write monitoring (called by memory write hooks)
    void onWrite32(uint32_t addr, uint32_t value);
    
    // Periodic scanning for vector tables (if no write hooks available)
    void poll();
    
    // Status queries
    bool isLocked() const { return m_locked; }
    uint32_t getVBR() const { return m_vbr_found; }
    uint16_t getBaseIndex() const { return m_base_index; }
    
    // Statistics
    size_t getTouchedPagesCount() const { return m_touch_pages.size(); }
    void clearTouchPages() { m_touch_pages.clear(); }

private:
    H8S2350Emulator& m_cpu;
    VecTrackCfg m_cfg;
    std::vector<uint32_t> m_touch_pages;
    bool m_locked;
    uint32_t m_vbr_found;
    uint16_t m_base_index;
    
    // Helper methods
    uint32_t readLong(uint32_t addr);
    bool isPlausibleCode(uint32_t vector) const;
    void setVBR(uint32_t vbr);
    void scanPageForVectors(uint32_t page);
};

} // namespace MS2000