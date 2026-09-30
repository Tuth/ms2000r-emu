#include "vector_table_tracker.h"
#include "h8s2350_emulator.h"

using namespace MS2000;

VectorTableTracker::VectorTableTracker(H8S2350Emulator& cpu, const VecTrackCfg& cfg)
    : m_cpu(cpu)
    , m_cfg(cfg)
    , m_locked(false)
    , m_vbr_found(0)
    , m_base_index(0x40)  // Default from readme.nfo
{
}

void VectorTableTracker::onWrite32(uint32_t addr, uint32_t value)
{
    if (m_locked) return;
    
    // Only track writes within RAM range
    if (addr < m_cfg.ramLo || addr + 3 > m_cfg.ramHi) return;
    
    // Only interested in 4-byte aligned writes (vector table entries)
    if (addr & 3) return;
    
    // Track the page (2KB aligned page) that was written to
    uint32_t page = addr & ~0x7FF;  // 2KB page boundary
    m_touch_pages.push_back(page);
    
    // Log first few writes for debugging
    if (m_touch_pages.size() <= 3) {
        std::cout << "[VectorTracker] RAM write detected: addr=0x" << std::hex << addr 
                  << ", value=0x" << value << ", page=0x" << page << std::dec << std::endl;
    }
    
    // Optional: trigger immediate scan if we get significant write activity
    if (m_touch_pages.size() > 5) {
        std::cout << "[VectorTracker] Triggering immediate scan after " 
                  << m_touch_pages.size() << " RAM writes" << std::endl;
        poll();
    }
}

void VectorTableTracker::poll()
{
    if (m_locked) return;
    
    // Sort and deduplicate touched pages
    std::sort(m_touch_pages.begin(), m_touch_pages.end());
    m_touch_pages.erase(std::unique(m_touch_pages.begin(), m_touch_pages.end()), 
                        m_touch_pages.end());
    
    // Scan each touched page for potential vector tables
    for (uint32_t page : m_touch_pages) {
        scanPageForVectors(page);
        if (m_locked) break;  // Found a table, stop scanning
    }
    
    // Clear touched pages to avoid re-scanning
    m_touch_pages.clear();
}

void VectorTableTracker::scanPageForVectors(uint32_t page)
{
    // Scan the 2KB page looking for vector table patterns
    for (uint32_t offset = 0; offset < 0x800; offset += 4) {
        // Try each base candidate at this offset
        for (uint16_t base : m_cfg.baseCandidates) {
            int valid_count = 0;
            
            // Check minValid consecutive entries starting at this base
            for (int i = 0; i < m_cfg.minValid; i++) {
                uint32_t addr = page + offset + 4u * (base + i);
                
                // Ensure we're still in RAM bounds
                if (addr < m_cfg.ramLo || addr + 3 > m_cfg.ramHi) break;
                
                uint32_t vector = readLong(addr);
                if (isPlausibleCode(vector)) {
                    valid_count++;
                }
            }
            
            // If we found enough valid vectors, this is our table!
            if (valid_count >= m_cfg.minValid) {
                uint32_t vbr = page + offset;
                setVBR(vbr);
                m_base_index = base;
                m_locked = true;
                
                std::cout << "[VectorTracker] 🎯 VECTOR TABLE FOUND!" << std::endl;
                std::cout << "  VBR: 0x" << std::hex << std::setfill('0') << std::setw(6) << vbr << std::dec << std::endl;
                std::cout << "  Base: 0x" << std::hex << base << std::dec << std::endl;
                std::cout << "  Valid vectors: " << valid_count << "/" << m_cfg.minValid << std::endl;
                
                return;
            }
        }
    }
}

uint32_t VectorTableTracker::readLong(uint32_t addr)
{
    // Delegate to CPU's memory reading system
    return m_cpu.readLong(addr);
}

bool VectorTableTracker::isPlausibleCode(uint32_t vector) const
{
    // Invalid values
    if (vector == 0 || vector == 0xFFFFFFFF) return false;
    
    // Must point to valid memory areas (ROM or RAM code regions)
    // ROM: 0x000000-0x0FFFFF
    if (vector >= 0x000000 && vector <= 0x0FFFFF) return true;
    
    // RAM: 0x100000-0x17FFFF (could contain copied code)
    if (vector >= 0x100000 && vector <= 0x17FFFF) return true;
    
    // External memory ranges if needed
    // if (vector >= 0x200000 && vector <= 0x2FFFFF) return true;
    
    return false;
}

void VectorTableTracker::setVBR(uint32_t vbr)
{
    m_vbr_found = vbr;

    // Set VBR in the CPU using the control register write method
    m_cpu.writeControlReg_VBR(vbr);

    // Check if TRAPA configuration is already locked (e.g., Mode 4 Advanced fixed config)
    auto current_trap_cfg = m_cpu.getTrapConfig();
    if (current_trap_cfg.locked) {
        std::cout << "[VectorTracker] TRAPA config already locked (base=" << (int)current_trap_cfg.baseIndex
                  << "), skipping autodetection override" << std::endl;
        std::cout << "[VectorTracker] ✅ Switched to NATIVE vector mode!" << std::endl;
        std::cout << "  TRAPA calls will use existing locked configuration" << std::endl;
        return;
    }

    // Lock the TRAP autodetection with our discovered base index (only if not already locked)
    TrapVectorConfig trap_cfg;
    trap_cfg.autoDetect = false;  // Disable autodetection now
    trap_cfg.baseIndex = m_base_index;
    trap_cfg.locked = true;       // Lock in native mode

    m_cpu.setTrapConfig(trap_cfg);

    std::cout << "[VectorTracker] ✅ Switched to NATIVE vector mode!" << std::endl;
    std::cout << "  TRAPA calls will now use real vectors instead of emulation" << std::endl;
}