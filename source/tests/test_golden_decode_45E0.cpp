// ============================================================================
// FIX-GOLD: Golden decode test - flash.bin 0x45E0-0x45FF window
// ============================================================================
// BLOCKING GATE (A2A protocol, STATE.json TASK-GOLD): every decoder change
// must pass this test before commit. The expected values below come from the
// Renesas H8S/2600 Hardware Manual (R5 ground truth), decoded by hand from
// the raw flash.bin bytes. They must NEVER be edited to match the decoder;
// if this test fails, the DECODER is wrong (or a genuine ISA discovery must
// be documented in STATE.json knowledge + cited to a manual page first).
//
// History: three fictional opcode families (BGE16/Bcc16, STC VBR, SHAR@1C)
// lived in this codebase because tests asserted the implementation instead
// of the manual. This test is the structural fix.
// ============================================================================

#include "../core/h8s2350_emulator.h"
#include "../core/h8s2350_instructions.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>

using namespace MS2000;

// Raw bytes from flash.bin offset 0x45E0 (SHA1 e878adc3...), 32 bytes.
static const uint8_t kGoldenBytes[32] = {
    0x6A, 0x2A, 0x00, 0x40, 0x27, 0x22,  // 0x45E0: MOV.B @0x402722:32, R2L
    0x6A, 0x28, 0x00, 0xFF, 0xF6, 0x14,  // 0x45E6: MOV.B @0xFFF614:32, R0L
    0x1A, 0x91,                          // 0x45EC: SUB.L group
    0x0C, 0xC9,                          // 0x45EE: MOV.B Rs,Rd
    0x11, 0x49,                          // 0x45F0: shift group (0x11)
    0x11, 0x09,                          // 0x45F2: shift group (0x11)
    0x7A, 0x11, 0x00, 0x40, 0x27, 0xC4,  // 0x45F4: ADD.L #0x4027C4, ER1
    0x7C, 0x10, 0x63, 0xC0,              // 0x45FA: BTST R4L, @ER1 (4 bytes!)
    0x47, 0x0E                           // 0x45FE: BEQ +0x0E
};

struct Expect {
    uint32_t offset;     // relative to window base
    uint16_t opcode;     // decoder's primary opcode value
    uint8_t  size;       // instruction length in bytes
    const char* tag;     // human label for failure messages
};

static const Expect kExpected[] = {
    { 0x00, 0x6A, 6, "MOV.B @aa:32,R2L"  },
    { 0x06, 0x6A, 6, "MOV.B @aa:32,R0L"  },
    { 0x0C, 0x1A, 2, "SUB.L group"       },
    { 0x0E, 0x0C, 2, "MOV.B Rs,Rd"       },
    { 0x10, 0x11, 2, "shift group #1"    },
    { 0x12, 0x11, 2, "shift group #2"    },
    { 0x14, 0x7A, 6, "ADD.L #imm32,ER1"  },
    { 0x1A, 0x7C, 4, "BTST Rn,@ERd"      },
    { 0x1E, 0x47, 2, "BEQ d:8"           },
};
static const int kCount = sizeof(kExpected) / sizeof(kExpected[0]);

static int run_at_base(H8S2350Emulator& emu, uint32_t base) {
    H8S2350InstructionDecoder dec;
    int failures = 0;

    uint32_t pc = base;
    for (int i = 0; i < kCount; ++i) {
        const Expect& e = kExpected[i];
        const uint32_t want_pc = base + e.offset;
        if (pc != want_pc) {
            std::printf("[GOLD-FAIL] #%d %s: PC drift - decoding at 0x%06X, manual says 0x%06X "
                        "(previous instruction size wrong)\n", i, e.tag, pc, want_pc);
            ++failures;
            pc = want_pc; // resync so remaining checks still run
        }
        auto ins = dec.decode(emu, pc);
        const uint16_t op = static_cast<uint16_t>(ins.opcode);
        if ((op & 0xFF) != e.opcode || ins.size != e.size) {
            std::printf("[GOLD-FAIL] #%d %s at 0x%06X: got opcode=0x%02X size=%u, "
                        "manual says opcode=0x%02X size=%u (mnemonic='%s')\n",
                        i, e.tag, pc, op & 0xFF, (unsigned)ins.size,
                        e.opcode, (unsigned)e.size,
                        ins.mnemonic.c_str());
            ++failures;
        }
        // Fiction guard: no decoded mnemonic in this window may reference the
        // purged families.
        if (ins.mnemonic.find("16") != std::string::npos ||
            ins.mnemonic.find("VBR") != std::string::npos ||
            ins.mnemonic == "SHAR.W") {
            std::printf("[GOLD-FAIL] #%d %s at 0x%06X: FICTION mnemonic '%s' resurfaced\n",
                        i, e.tag, pc, ins.mnemonic.c_str());
            ++failures;
        }
        pc += e.size;
    }
    if (pc != base + 0x20) {
        std::printf("[GOLD-FAIL] window total: ended at 0x%06X, manual says 0x%06X "
                    "(sizes must sum to 32)\n", pc, base + 0x20);
        ++failures;
    }
    return failures;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: survive crashes
    setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("=== FIX-GOLD: golden decode 0x45E0-0x45FF (R5 manual ground truth) ===\n");

    // NOTE: H8S2350Emulator is larger than the 1 MB default MSVC stack
    // (SIMD caches) -> stack instantiation = 0xC00000FD before main runs.
    // This is why ALL ms2000-linked tests "segfaulted". Heap-allocate.
    std::printf("[GOLD-STEP] constructing emulator (heap)...\n");
    auto emu_owner = std::make_unique<H8S2350Emulator>();
    H8S2350Emulator& emu = *emu_owner;
    std::printf("[GOLD-STEP] reset()...\n");
    emu.reset();
    std::printf("[GOLD-STEP] writing golden bytes...\n");

    // HARNESS FIX 2026-09-26 (Tamas OK'd fixing useful ctests): 0x000000-0x0FFFFF is the
    // MBM29LV800B flash since BUG102 - a CPU bus write there is a command cycle, not a store,
    // exactly as on the board, so the bytes can never be planted at 0x45E0. The decode is
    // base-relative (every want_pc = base + offset), so the SAME 32 flash.bin bytes are placed
    // in the external DRAM at 0x400100 - where test_bug122_mov / test_bitmem_abs run code.
    // The assertions are unchanged.
    const uint32_t base = 0x400100;
    for (uint32_t i = 0; i < 32; ++i) emu.writeByte(base + i, kGoldenBytes[i]);
    for (uint32_t i = 0; i < 32; ++i) {
        if (emu.readByte(base + i) != kGoldenBytes[i]) {
            std::printf("[GOLD-FAIL] memory not writable at base 0x%06X - "
                        "fix test harness, do NOT waive the gate\n", base);
            return 2;
        }
    }

    const int failures = run_at_base(emu, base);
    if (failures == 0) {
        std::printf("[GOLD-PASS] 9/9 instructions decode per manual, window sums to 32 bytes\n");
        return 0;
    }
    std::printf("[GOLD-RESULT] %d failure(s) - decoder violates manual (R5)\n", failures);
    return 1;
}
