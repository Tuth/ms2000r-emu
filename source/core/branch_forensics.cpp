#include "branch_forensics.h"
#include "h8s2350_emulator.h"
#include <cstdio>
#include <cinttypes>

namespace MS2000 {

std::vector<BranchDecisionSnapshot> g_branch_forensics;

uint64_t computeStateHash(const H8S2350Emulator& emu) {
    // Simple but effective hash combining all micro-state
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    auto& regs = emu.getRegisters();
    
    // Registers
    for (int i = 0; i < 8; i++) {
        h ^= regs.er[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= regs.r[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= regs.rl[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    
    // Status - CCR is in registers.ccr (uint16_t), EXR in registers.exr
    h ^= regs.ccr + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= regs.exr + 0x9e3779b9 + (h << 6) + (h >> 2);
    
    // Stack pointer
    h ^= regs.sp + 0x9e3779b9 + (h << 6) + (h >> 2);
    
    return h;
}

void logBranchDecision(uint32_t pc, bool taken, int16_t disp, uint32_t target) {
    // We need access to emulator state - this will be called from emulator
    // For now, store what we have
    BranchDecisionSnapshot snap{};
    snap.pc = pc;
    snap.branch_taken = taken;
    snap.displacement = disp;
    snap.target_pc = target;
    snap.state_hash = 0; // Will be filled by caller
    
    g_branch_forensics.push_back(snap);
    
    // Keep only last 100 decisions
    if (g_branch_forensics.size() > 100) {
        g_branch_forensics.erase(g_branch_forensics.begin());
    }
}

void dumpBranchForensics() {
    printf("\n===== BRANCH FORENSICS DUMP (%zu decisions) =====\n", g_branch_forensics.size());
    for (const auto& s : g_branch_forensics) {
        printf("PC=0x%06X | TAKEN=%d | DISP=%+d | TARGET=0x%06X | HASH=%016" PRIx64 "\n",
               s.pc, s.branch_taken, s.displacement, s.target_pc, s.state_hash);
    }
    printf("========================================\n\n");
}

} // namespace MS2000
