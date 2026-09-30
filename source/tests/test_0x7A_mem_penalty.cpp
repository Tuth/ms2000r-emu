// tests/test_0x7A_mem_penalty.cpp
// 0x7A @aa:32 RAM/EXT regression & cycle penalty guard
#include "core/h8s2350_emulator.h"
#include "core/h8s2350_contracts.h"
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace MS2000;

// HARNESS FIX 2026-09-26. This test was written against a FICTION that CLAUDE.md already
// retired from the decoder: 0x7A is the 32-bit IMMEDIATE group (7A 0 = MOV.L #xx:32, 7A 1 =
// ADD.L #xx:32 ... HM RENDERED p.794-806), not "MOV.W with @aa:32". The real word forms are
// MOV.W Rs,@aa:32 = 6B A:rs abs32 and MOV.W @aa:32,Rd = 6B 2:rd abs32 (the MOV.L table in
// CLAUDE.md, same 6B 2/6B A rows behind the 01 00 prefix; the firmware's own 407274:
// 6B 20 00 40 60 18 = MOV.W @0x406018,R0). Name kept so the ctest history stays readable;
// the intent is unchanged: an @aa:32 word store+load round-trips, and external memory
// costs more states than on-chip RAM.
static void emit_storeW_R0_to_abs32(std::vector<uint8_t>& buf, uint32_t a32) {
    buf.push_back(0x6B);
    buf.push_back(0xA0 | 0x0);           // MOV.W R0,@aa:32
    buf.push_back((a32 >> 24) & 0xFF);
    buf.push_back((a32 >> 16) & 0xFF);
    buf.push_back((a32 >> 8)  & 0xFF);
    buf.push_back(a32 & 0xFF);
}

static void emit_loadW_abs32_to_R1(std::vector<uint8_t>& buf, uint32_t a32) {
    buf.push_back(0x6B);
    buf.push_back(0x20 | 0x1);           // MOV.W @aa:32,R1
    buf.push_back((a32 >> 24) & 0xFF);
    buf.push_back((a32 >> 16) & 0xFF);
    buf.push_back((a32 >> 8)  & 0xFF);
    buf.push_back(a32 & 0xFF);
}

// Helper to install code into memory
static void install_code(H8S2350Emulator& emu, uint32_t pc, const std::vector<uint8_t>& code) {
    for (size_t i = 0; i < code.size(); ++i) {
        emu.writeByte((pcMask24(pc) + (uint32_t)i), code[i]);
    }
}

// Test execution helper (based on memory_penalty_test.cpp)
struct MeasureResult {
    uint64_t cycles = 0;
    uint32_t readback = 0;
};

static MeasureResult run_one(H8S2350Emulator& emu, uint32_t pc, uint32_t a32_target,
                             uint32_t value_to_store) {
    // Micro-program: MOV.W R0->@aa:32 ; MOV.W @aa:32->R1
    std::vector<uint8_t> code;
    emit_storeW_R0_to_abs32(code, a32_target);
    emit_loadW_abs32_to_R1(code, a32_target);

    // Install micro-program into internal RAM at PC
    install_code(emu, pc, code);

    // Init regs
    emu.setProgramCounter(pc);
    emu.setERd(0, value_to_store); // R0 low word = value to store
    emu.setERd(1, 0x00000000);     // R1 low word = will hold the load result

    // Measure exactly the two instructions (store+load)
    uint64_t start = emu.getCycles();
    emu.step(); // store W
    emu.step(); // load  W
    uint64_t end = emu.getCycles();

    MeasureResult r;
    r.cycles = end - start;
    r.readback = emu.getRegisters().er[1] & 0xFFFFu;
    return r;
}

int main() {
    H8S2350Emulator emu;

    // So the test is deterministic & isolated
    emu.reset();
    // Put stack & VBR into valid internal RAM ranges (not strictly needed here, but tidy)
    emu.setSP24(0x00FFFC00 - 4);           // top of on-chip RAM (was a 32-bit-mode 0xF82000)

    // Test addresses
    // Addresses the board decodes (CLAUDE.md BUG59 / memory map): on-chip RAM
    // 0xFFF400-0xFFFBFF, DRAM 0x400000-0x47FFFF. The old 0xF80800 / 0x100000 were
    // 32-bit-mode constants; nothing answers there in advanced mode.
    const uint32_t A32_RAM = 0x00FFF800;   // on-chip RAM
    const uint32_t A32_EXT = 0x00404000;   // external DRAM (area 2)
    // HARNESS FIX 2026-09-26: the micro-program used to be planted at 0x001000, which is the
    // MBM29LV800B flash since BUG102 (bus writes = command cycles, nothing stored), so the CPU
    // fetched 0xFF and never ran it. It now lives in the external DRAM, like the other tests
    // that execute planted code (test_bug122_mov, test_bitmem_abs). Assertions unchanged.
    const uint32_t PC_MICRO = 0x400100;    // place microprogram in external DRAM
    const uint32_t VAL = 0x0000BEEF;

    // RAM run
    auto ram = run_one(emu, PC_MICRO, A32_RAM, VAL);
    // EXT run
    auto ext = run_one(emu, PC_MICRO + 0x40, A32_EXT, VAL);

    // Functional checks
    if (ram.readback != VAL) {
        std::printf("[TEST-FAIL] 0x7A RAM round-trip: got 0x%04X, want 0x%04X\n",
                    ram.readback, VAL);
        return 1;
    }
    if (ext.readback != VAL) {
        std::printf("[TEST-FAIL] 0x7A EXT round-trip: got 0x%04X, want 0x%04X\n",
                    ext.readback, VAL);
        return 1;
    }

    // Cycle delta check: EXT must be strictly slower (HM Table A.4: on-chip memory 1 state per
    // access, external areas 2-state or 3+m). As of 2026-09-26 THIS FAILS FOR A REAL REASON, not
    // a harness one: fetchExtraStates() (BUG105) charges external states for the instruction
    // FETCH only; data accesses (Table A.5 L/M) are still the on-chip figure - its "STATED
    // APPROXIMATION (1)". Left failing on purpose until data-access states are modelled.
    std::printf("[TEST-INFO] cycles: RAM=%llu EXT=%llu (delta=%lld)\n",
                (unsigned long long)ram.cycles,
                (unsigned long long)ext.cycles,
                (long long)(ext.cycles - ram.cycles));

    if (!(ext.cycles > ram.cycles)) {
        std::printf("[TEST-FAIL] Expected EXT > RAM cycles.\n");
        return 1;
    }

    std::printf("[TEST-PASS] 0x7A @aa:32 RAM/EXT regression guard\n");
    return 0;
}
