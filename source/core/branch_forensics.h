#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "h8s2350_emulator.h"

namespace MS2000 {

struct BranchDecisionSnapshot {
    uint32_t pc;
    
    // Register state
    uint32_t er[8];    // ER0-ER7
    uint8_t r[8];      // R0L-R7L
    uint8_t rl[8];     // RL0-RL7
    
    // Status
    uint8_t ccr;
    uint8_t exr;
    
    // Stack
    uint32_t sp;
    uint8_t stack_top[16];
    
    // Memory context (last 3 reads)
    struct MemAccess { uint32_t addr; uint32_t val; };
    MemAccess last_reads[3];
    
    // Branch info
    bool branch_taken;
    int16_t displacement;
    uint32_t target_pc;
    
    // Hash for determinism check
    uint64_t state_hash;
};

// Declare global forensics collector
extern std::vector<BranchDecisionSnapshot> g_branch_forensics;

// Call this at every branch decision point
void logBranchDecision(uint32_t pc, bool taken, int16_t disp, uint32_t target);

// Compute determinism hash from micro-state
uint64_t computeStateHash(const H8S2350Emulator& emu);

// Dump forensics report
void dumpBranchForensics();

} // namespace MS2000
