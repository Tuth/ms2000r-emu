#pragma once
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

// fw5.txt I/O scanner to find memory-mapped LCD addresses
struct IoHotspot { 
    uint32_t addr; 
    uint32_t r = 0, w = 0; 
};

class IoProbe {
private:
    static inline std::unordered_map<uint32_t, IoHotspot> g_io;
    static inline bool enabled = false;
    
public:
    static inline void enable() { enabled = true; }
    static inline void disable() { enabled = false; }
    
    static inline void log_read(uint32_t a) { 
        if (!enabled) return;
        auto& h = g_io[a]; 
        h.addr = a; 
        h.r++; 
    }
    
    static inline void log_write(uint32_t a) { 
        if (!enabled) return;
        auto& h = g_io[a]; 
        h.addr = a; 
        h.w++; 
    }
    
    // BUG78: THE CAP IS SETTABLE NOW - MS2K_IOTOP=<n>, default 12 as before.
    //
    // It cost a measurement this round. Mapping the A/D pushed 0xFFFF98 (Total=3416) and
    // the four ADDR pairs out of a TWELVE-entry list, and the list is all there is - so
    // the honest statement was "it fell below 267", the floor of the printed window,
    // rather than a number. This project already has the rule from BUG71: A CAPPED COUNTER
    // CANNOT MEASURE A DIFFERENCE. The map keeps every address; only the print was capped.
    static inline void dump_top(int count = 16) {
        if (const char* e = std::getenv("MS2K_IOTOP")) {
            const int n = std::atoi(e);
            if (n > 0) count = n;
        }
        std::vector<IoHotspot> v;
        v.reserve(g_io.size());
        for (auto& kv : g_io) v.push_back(kv.second);

        std::sort(v.begin(), v.end(), [](auto& A, auto& B){
            return (A.r + A.w) > (B.r + B.w);
        });

        printf("[IO-SCAN] Top %d of %zu I/O addresses touched:\n", count, v.size());
        for (size_t i = 0; i < std::min<size_t>(v.size(), count); ++i) {
            printf("[IO] %02zu: 0x%06X  R=%u W=%u  Total=%u\n",
                   i, v[i].addr, v[i].r, v[i].w, v[i].r + v[i].w);
        }

        // BUG78: what used to stand here printed the same list again as
        // "[LCD-CANDIDATE] ... (likely STATUS/CTRL | DATA/CTRL)", guessing the role from
        // whether the ADDRESS WAS EVEN. It was labelling the DMAC's MAR0B bytes and the
        // A/D's ADDRA-D as candidate LCD registers.
        //
        // THE LCD QUESTION IS CLOSED. KOD-A30411 puts the display on PORT 2 as direct
        // GPIO (BUG44/BUG47) and BUG48 removed five invented wirings that grew out of
        // exactly this kind of label. A guess printed in the same log as a measurement
        // becomes a measurement to the next reader - BUG70b, where P1DR spent a month
        // being called "LCD Control". The list above is what it always was: hot I/O
        // addresses. Nothing here names a peripheral.
    }
    
    static inline void clear() { g_io.clear(); }
};