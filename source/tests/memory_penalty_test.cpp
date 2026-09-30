// Sprint 1.3: RAM vs EXT cycle validation (deterministic)
// DoD: same code on EXT takes strictly more cycles than on internal RAM
#include "core/h8s2350_emulator.h"
#include "core/h8s2350_contracts.h"
#include "core/h8s2350_debug.h"
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cassert>

using namespace MS2000;

// 0x7A group encoding quick ref used here:
//  store L: 0x7A, sub = 0x20 | r   (ERr -> @aa:24), then 3-byte addr (big-endian)
//  load  L: 0x7A, sub = 0x60 | r   (@aa:24 -> ERr), then 3-byte addr
static void emit_storeL_ER0_to_abs24(std::vector<uint8_t>& buf, uint32_t a24) {
    buf.push_back(0x7A);
    buf.push_back(0x20 | 0x0);           // op=2 (L store), r=ER0
    buf.push_back((a24 >> 16) & 0xFF);
    buf.push_back((a24 >> 8)  & 0xFF);
    buf.push_back(a24 & 0xFF);
}
static void emit_loadL_abs24_to_ER1(std::vector<uint8_t>& buf, uint32_t a24) {
    buf.push_back(0x7A);
    buf.push_back(0x60 | 0x1);           // op=6 (L load), r=ER1
    buf.push_back((a24 >> 16) & 0xFF);
    buf.push_back((a24 >> 8)  & 0xFF);
    buf.push_back(a24 & 0xFF);
}
// optional: tiny endless loop to stop execution cleanly after our two insns
static void emit_bra_self(std::vector<uint8_t>& buf) {
    // BRA.b disp8 = 0x20, disp = -2 (0xFE) → endless 2-byte loop
    buf.push_back(0x20);
    buf.push_back(0xFE);
}

struct MeasureResult {
    uint64_t cycles = 0;
    uint32_t readback = 0;
};

static MeasureResult run_one(H8S2350Emulator& emu, uint32_t pc, uint32_t a24_target,
                             uint32_t value_to_store) {
    // Micro-program: MOV.L ER0->@aa:24 ; MOV.L @aa:24->ER1 ; BRA .-2
    std::vector<uint8_t> code;
    emit_storeL_ER0_to_abs24(code, a24_target);
    emit_loadL_abs24_to_ER1(code, a24_target);
    emit_bra_self(code);

    // Install micro-program into internal RAM at PC
    for (size_t i = 0; i < code.size(); ++i) {
        emu.writeByte( (pcMask24(pc) + (uint32_t)i), code[i] );
    }

    // Init regs
    emu.setProgramCounter(pc);
    emu.setERd(0, value_to_store); // ER0 = value to store
    emu.setERd(1, 0x00000000);     // ER1 = will hold the load result

    // Measure exactly the two instructions (store+load); then we land in BRA loop
    uint64_t start = emu.getCycles();
    // step() name may differ in your codebase; if you use a "runSteps(n)", swap accordingly.
    emu.step(); // store L
    emu.step(); // load  L
    uint64_t end = emu.getCycles();

    MeasureResult r;
    r.cycles = end - start;
    r.readback = emu.getRegisters().er[1];
    return r;
}

int main() {
    H8S2350Emulator emu;

    // So the test is deterministic & isolated
    emu.reset();
    // Put stack & VBR into valid internal RAM ranges (not strictly needed here, but tidy)
    emu.setSP24(0x00F82000 - 4);
    emu.setVBR(0x00F80000);

    // Test addresses
    const uint32_t A24_RAM = 0x00F80800;   // internal RAM (→ phys 0xFFF80800)
    const uint32_t A24_EXT = 0x00100000;   // external memory
    const uint32_t PC_MICRO = 0x001000;    // place microprogram in RAM
    const uint32_t VAL = 0xDEADBEEF;

    // RAM run
    auto ram = run_one(emu, PC_MICRO, A24_RAM, VAL);
    // EXT run
    auto ext = run_one(emu, PC_MICRO + 0x40, A24_EXT, VAL);

    // Functional checks
    if (ram.readback != VAL) {
        std::printf("[MEM-PENALTY][FAIL] RAM readback mismatch: got 0x%08X, want 0x%08X\n",
                    ram.readback, VAL);
        return 1;
    }
    if (ext.readback != VAL) {
        std::printf("[MEM-PENALTY][FAIL] EXT readback mismatch: got 0x%08X, want 0x%08X\n",
                    ext.readback, VAL);
        return 1;
    }

    // Cycle delta check — DoD: EXT must be strictly slower
    std::printf("[MEM-PENALTY] cycles: RAM=%llu EXT=%llu (delta=%lld)\n",
                (unsigned long long)ram.cycles,
                (unsigned long long)ext.cycles,
                (long long)(ext.cycles - ram.cycles));

    if (!(ext.cycles > ram.cycles)) {
        std::printf("[MEM-PENALTY][FAIL] Expected EXT > RAM cycles.\n");
        return 1;
    }

    std::printf("[MEM-PENALTY][PASS] EXT is slower than RAM as expected.\n");
    return 0;
}