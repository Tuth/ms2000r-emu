#pragma once
#include <cstdint>

// ---- H8S/2350 architectural contracts ----

// Forward declarations for table-driven decoder
namespace MS2000 {
    class H8S2350Emulator;
    struct H8S2350Instruction;
}

// 24-bit logical address mask
static inline uint32_t pcMask24(uint32_t pc) { return pc & 0x00FFFFFFu; }

// Internal RAM map (logical ↔ physical)
constexpr uint32_t H8S_RAM_LOG_START  = 0x00F80000u;
constexpr uint32_t H8S_RAM_SIZE       = 0x00002000u; // 8 KB
constexpr uint32_t H8S_RAM_PHYS_START = 0xFFF80000u;

// Cycle model constants
static constexpr uint32_t H8S_CYC_BASE_ALU      = 1;
static constexpr uint32_t H8S_CYC_BASE_MEM      = 1;   // memory access base cost
static constexpr uint32_t H8S_CYC_BRANCH_TAKEN  = 1;   // feltételes ág: +1 ha taken
static constexpr uint32_t H8S_CYC_MEM_READ_RAM  = 1;
static constexpr uint32_t H8S_CYC_MEM_WRITE_RAM = 1;
static constexpr uint32_t H8S_CYC_MEM_READ_ROM  = 1;
static constexpr uint32_t H8S_CYC_MEM_READ_IO   = 2;
static constexpr uint32_t H8S_CYC_PUSH_BYTE     = 1;   // stack művelet / bájt
static constexpr uint32_t H8S_CYC_POP_BYTE      = 1;

// Extended Branch Instructions (P1 specification)
// ================================================
// BLE16 (0x007F, 4 bytes): Branch if Less or Equal
// - Format: 0x00 0x7F disp_hi disp_lo
// - next_pc = pc + 4
// - disp16: big-endian, signed 16-bit displacement
// - condition: (Z == 1) || (N != V)
// - target = pcMask24(next_pc + (int32_t)disp16)
// - cycles: base 2 + (taken ? 1 : 0) = 2 not-taken, 3 taken
// - Implementation: Early intercept at h8s2350_instructions.cpp:1394-1418
//
// Extended Branch Family (P1 - IMPLEMENTED) - H8S/2600 Manual:
// - BGE16 (0x007C): (N == V) - Branch if Greater or Equal  
// - BLT16 (0x007D): (N != V) - Branch if Less Than
// - BGT16 (0x007E): !Z && (N == V) - Branch if Greater Than
// - BLE16 (0x007F): Z || (N != V) - Branch if Less or Equal
// All use same displacement/target calculation and timing

// Extended Branch Utility Functions
// =================================
// Forward declaration for emulator access
namespace MS2000 { class H8S2350Emulator; }

// Read big-endian signed 16-bit displacement from 4-byte extended branch
// Format: [0x00 op_lo disp_hi disp_lo] at start_pc
static inline int16_t be_disp16(MS2000::H8S2350Emulator& emu, uint32_t start_pc);

// Calculate 24-bit masked branch target
static inline uint32_t ext_branch_target(uint32_t next_pc, int16_t disp16) {
    return pcMask24(next_pc + (int32_t)disp16);
}

// Sprint 1.3: Centralized memory domain classification and penalty system
enum class MemDomain { 
    RAM,    // Internal H8S RAM - fastest access
    ROM,    // Internal ROM/Flash - baseline performance  
    EXT,    // External memory - slower access
    IO      // Memory-mapped I/O - special handling
};

// Centralized memory penalty calculation (scalable by bytes)
static inline uint32_t memReadPenalty(MemDomain d, uint32_t bytes = 1) {
    switch (d) {
        case MemDomain::RAM: return 0u * bytes;   // Internal RAM: 0 cycles/byte
        case MemDomain::ROM: return 0u * bytes;   // Internal ROM: 0 cycles/byte (baseline)
        case MemDomain::EXT: return 2u * bytes;   // External memory: +2 cycles/byte
        case MemDomain::IO:  return 1u;           // MMIO: +1 per access (not scaled by bytes)
    }
    return 0;
}

static inline uint32_t memWritePenalty(MemDomain d, uint32_t bytes = 1) {
    switch (d) {
        case MemDomain::RAM: return 0u * bytes;   // Internal RAM: 0 cycles/byte  
        case MemDomain::ROM: return 0u;           // ROM not writable, but keep 0 for safety
        case MemDomain::EXT: return 2u * bytes;   // External memory: +2 cycles/byte
        case MemDomain::IO:  return 1u;           // MMIO: +1 per access
    }
    return 0;
}

// Memory domain classification based on physical address  
static inline MemDomain classifyDomain(uint32_t phys_addr) {
    // H8S/2350 internal RAM: 0x00F80000-0x00F81FFF (8KB, 24-bit addressing)
    if (phys_addr >= 0x00F80000u && phys_addr < 0x00F82000u) return MemDomain::RAM;
    
    // H8S on-chip I/O window: 0x00FF0000-0x00FFFFFF  
    if ((phys_addr & 0x00FF0000u) == 0x00FF0000u) return MemDomain::IO;
    
    // Internal ROM/Flash: 0x00000000-0x000FFFFF (1MB direct mapping)
    if (phys_addr <= 0x000FFFFFu) return MemDomain::ROM;
    
    // External DRAM: 0x00100000-0x004FFFFF (4 MiB)
    if (phys_addr >= 0x00100000u && phys_addr < 0x00500000u) return MemDomain::EXT;
    
    // Everything else defaults to external memory
    return MemDomain::EXT;
}

// Note: stackPhys() is implemented in h8s2350_emulator.h

// ===== EXTENDED BRANCH FAMILY (P1 PRODUCTION) =====
// Opcodes: 0x006F-0x007E (16 instructions) 
// Format: 0x00 <opcode> disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

// Extended Branch Condition Truth Table (H8S Manual):
// | Opcode | Instr  | Condition      | Description              |
// |--------|--------|----------------|--------------------------|
// ===== EXTENDED BRANCH FAMILY (P1 PRODUCTION) =====
// Opcodes: 0x006F-0x007A (16 instructions) 
// Format: 0x00 <opcode> disp_hi disp_lo
// Timing: 2 base + (taken ? 1 : 0) cycles

// Extended Branch Condition Truth Table (H8S Manual):
// | Opcode | Instr  | Condition      | Description              |
// |--------|--------|----------------|--------------------------|
// | 0x006F | BRA16  | 1              | Branch Always            |
// | 0x0070 | BRN16  | 0              | Branch Never             |
// | 0x0071 | BHI16  | !C && !Z       | Branch Higher (unsigned) |
// | 0x0072 | BLS16  | C || Z         | Branch Lower/Same (unsigned) |
// | 0x0073 | BCC16  | !C             | Branch Carry Clear       |
// | 0x0074 | BCS16  | C              | Branch Carry Set         |
// | 0x0075 | BNE16  | !Z             | Branch Not Equal         |
// | 0x0076 | BEQ16  | Z              | Branch Equal             |
// | 0x0077 | BVC16  | !V             | Branch Overflow Clear    |
// | 0x0078 | BVS16  | V              | Branch Overflow Set      |
// | 0x0079 | BPL16  | !N             | Branch Plus              |
// | 0x007A | BMI16  | N              | Branch Minus             |

// ===== EXECUTION PATH TRACKING (Release-safe hardening) =====
enum class ExecPath : uint8_t {
    Unknown = 0,
    Intercept = 1,    // Early intercept path (optimized)  
    Fallback = 2      // Legacy fallback path (should never occur for extended branches)
};

// ===== DESIGN NOTE: EXTENDED BRANCH ARCHITECTURE =====
// Extended branches (0x64-0x7F) → early intercept; 4-byte form; big-endian signed disp16; 
// target = pcNext + disp16 (24-bit mask); cycles = 2 + taken.
// Release-mode early-intercept stabilized; fallback never active.

// ===== SINGLE SOURCE OF TRUTH (SSoT) FOR EXTENDED BRANCHES =====
namespace H8S { namespace ExtBr {
    
    // Opcodes (centrální forrás - EGYETLEN IGAZSÁG)
    constexpr uint16_t BRA16 = 0x006F;  // Branch Always  
    constexpr uint16_t BRN16 = 0x0070;  // Branch Never
    constexpr uint16_t BEQ16 = 0x0064;  // Branch if Equal (Z) - H8S/2600 Manual
    constexpr uint16_t BNE16 = 0x0065;  // Branch if Not Equal (!Z) - H8S/2600 Manual
    constexpr uint16_t BCC16 = 0x0066;  // Branch if Carry Clear (!C) / BHS - H8S/2600 Manual
    constexpr uint16_t BCS16 = 0x0067;  // Branch if Carry Set (C) / BLO - H8S/2600 Manual
    constexpr uint16_t BHI16 = 0x0068;  // Branch if Higher (!C && !Z) - H8S/2600 Manual
    constexpr uint16_t BLS16 = 0x0069;  // Branch if Lower or Same (C || Z) - H8S/2600 Manual
    constexpr uint16_t BPL16 = 0x006A;  // Branch if Plus (!N) - H8S/2600 Manual
    constexpr uint16_t BMI16_P15 = 0x006B;  // Branch if Minus (N) - H8S/2600 Manual P1.5
    constexpr uint16_t BVC16 = 0x0077;  // Branch Overflow Clear
    constexpr uint16_t BVS16 = 0x0078;  // Branch Overflow Set
    constexpr uint16_t BMI16 = 0x007A;  // Branch Minus
    
    // NOTE: 0x7C-0x7F are NOT extended branches in H8S ISA
    // They are bit-manipulation groups @ERd/@aa: 
    // 0x7C/7E = bit-TEST groups (BTST Rn,@ERd / BTST #imm,@ERd)
    // 0x7D/7F = bit-manipulation BSET/BCLR/BNOT/etc. store-forms
    // Real 16-bit Bcc = 0x58 [cc]0 disp16 (decoded separately in instructions.cpp)
    
    // Condition functions (signed compare, CCR: Z,N,V,C) - forceinline for Release stability
    [[msvc::forceinline]] constexpr inline bool cond_bra(bool Z, bool N, bool V, bool C) { (void)Z; (void)N; (void)V; (void)C; return true; }
    [[msvc::forceinline]] constexpr inline bool cond_brn(bool Z, bool N, bool V, bool C) { (void)Z; (void)N; (void)V; (void)C; return false; }
    [[msvc::forceinline]] constexpr inline bool cond_bhi(bool Z, bool N, bool V, bool C) { (void)N; (void)V; return !C && !Z; }
    [[msvc::forceinline]] constexpr inline bool cond_bls(bool Z, bool N, bool V, bool C) { (void)N; (void)V; return C || Z; }
    [[msvc::forceinline]] constexpr inline bool cond_bcc(bool Z, bool N, bool V, bool C) { (void)Z; (void)N; (void)V; (void)C; return !C; }
    [[msvc::forceinline]] constexpr inline bool cond_bcs(bool Z, bool N, bool V, bool C) { (void)Z; (void)N; (void)V; (void)C; return C; }
    [[msvc::forceinline]] constexpr inline bool cond_bne(bool Z, bool N, bool V, bool C) { (void)N; (void)V; (void)C; return !Z; }
    [[msvc::forceinline]] constexpr inline bool cond_beq(bool Z, bool N, bool V, bool C) { (void)N; (void)V; (void)C; return Z; }
    [[msvc::forceinline]] constexpr inline bool cond_bvc(bool Z, bool N, bool V, bool C) { (void)Z; (void)N; (void)V; (void)C; return !V; }
    [[msvc::forceinline]] constexpr inline bool cond_bvs(bool Z, bool N, bool V, bool C) { (void)Z; (void)N; (void)V; (void)C; return V; }
    [[msvc::forceinline]] constexpr inline bool cond_bpl(bool Z, bool N, bool V, bool C) { (void)Z; (void)V; (void)C; return !N; }
    [[msvc::forceinline]] constexpr inline bool cond_bmi(bool Z, bool N, bool V, bool C) { (void)Z; (void)V; (void)C; return N; }
    
    // DELETED: cond_bge, cond_blt, cond_bgt, cond_ble - these opcodes (0x7C-0x7F) are bit-manipulation in H8S, not extended branches
    
    // Mnemonic lookup (compile-time)
    constexpr inline const char* mnemonic(uint16_t opc) {
        switch (opc) {
            case BRA16: return "BRA16";
            case BRN16: return "BRN16";
            case BHI16: return "BHI16";
            case BLS16: return "BLS16";
            case BCC16: return "BCC16";
            case BCS16: return "BCS16";
            case BNE16: return "BNE16";
            case BEQ16: return "BEQ16";
            case BVC16: return "BVC16";
            case BVS16: return "BVS16";
            case BPL16: return "BPL16";
            case BMI16: return "BMI16";
            default:    return "EXTBR?";
        }
    }
    
    // Condition evaluator (runtime dispatch)
    constexpr inline bool evaluate(uint16_t opc, bool Z, bool N, bool V, bool C) {
        switch (opc) {
            case BRA16: return cond_bra(Z, N, V, C);
            case BRN16: return cond_brn(Z, N, V, C);
            case BHI16: return cond_bhi(Z, N, V, C);
            case BLS16: return cond_bls(Z, N, V, C);
            case BCC16: return cond_bcc(Z, N, V, C);
            case BCS16: return cond_bcs(Z, N, V, C);
            case BNE16: return cond_bne(Z, N, V, C);
            case BEQ16: return cond_beq(Z, N, V, C);
            case BVC16: return cond_bvc(Z, N, V, C);
            case BVS16: return cond_bvs(Z, N, V, C);
            case BPL16: return cond_bpl(Z, N, V, C);
            case BMI16: return cond_bmi(Z, N, V, C);
            default:    return false;
        }
    }
    
    // ===== COMPREHENSIVE OPCODE UNIQUENESS PROTECTION (0x64-0x7F) =====
    // This is the Single Source of Truth (SSoT) guardrail system
    
    // P1.5 Extended Branch Family (0x64-0x6B) - IMPLEMENTED & TESTED
    static_assert(BEQ16 == 0x0064, "BEQ16 must be 0x0064 per H8S/2600 manual");
    static_assert(BNE16 == 0x0065, "BNE16 must be 0x0065 per H8S/2600 manual");
    static_assert(BCC16 == 0x0066, "BCC16 must be 0x0066 per H8S/2600 manual");
    static_assert(BCS16 == 0x0067, "BCS16 must be 0x0067 per H8S/2600 manual");
    static_assert(BHI16 == 0x0068, "BHI16 must be 0x0068 per H8S/2600 manual");
    static_assert(BLS16 == 0x0069, "BLS16 must be 0x0069 per H8S/2600 manual");
    static_assert(BPL16 == 0x006A, "BPL16 must be 0x006A per H8S/2600 manual");
    static_assert(BMI16_P15 == 0x006B, "BMI16_P15 must be 0x006B per H8S/2600 manual");
    
    // Reserved/Future opcodes (0x006C-0x006E)
    // static_assert for future use when implemented
    
    // P1 Extended Branch Family (0x6F, 0x70-0x7A) - IMPLEMENTED & TESTED
    // NOTE: 0x7B-0x7F are NOT extended branches in H8S ISA
    // 0x7B = STC VBR (fictional - doesn't exist in H8S)
    // 0x7C-0x7F = bit-manipulation groups @ERd/@aa
    static_assert(BRA16 == 0x006F, "BRA16 must be 0x006F per H8S/2600 manual");
    static_assert(BRN16 == 0x0070, "BRN16 must be 0x0070 per H8S/2600 manual");
    static_assert(BVC16 == 0x0077, "BVC16 must be 0x0077 per H8S/2600 manual");
    static_assert(BVS16 == 0x0078, "BVS16 must be 0x0078 per H8S/2600 manual");
    static_assert(BMI16 == 0x007A, "BMI16 must be 0x007A per H8S/2600 manual");
    
    // Cross-family uniqueness verification
    static_assert(BEQ16 != BNE16 && BEQ16 != BCC16 && BEQ16 != BCS16, "P1.5 uniqueness #1");
    static_assert(BHI16 != BLS16 && BPL16 != BMI16_P15, "P1.5 uniqueness #2");
    static_assert(BRA16 != BRN16 && BVC16 != BVS16, "P1 uniqueness #2");
    static_assert(BMI16 != BMI16_P15, "BMI16 0x7A vs BMI16_P15 0x6B distinct");
    
    // Range validation - all extended branches must be in 0x64-0x7A range (0x7B-0x7F are NOT extended branches)
    static_assert(BEQ16 >= 0x0064 && BEQ16 <= 0x007A, "BEQ16 in extended range");
    static_assert(BNE16 >= 0x0064 && BNE16 <= 0x007A, "BNE16 in extended range");
    static_assert(BCC16 >= 0x0064 && BCC16 <= 0x007A, "BCC16 in extended range");
    static_assert(BCS16 >= 0x0064 && BCS16 <= 0x007A, "BCS16 in extended range");
    static_assert(BHI16 >= 0x0064 && BHI16 <= 0x007A, "BHI16 in extended range");
    static_assert(BLS16 >= 0x0064 && BLS16 <= 0x007A, "BLS16 in extended range");
    static_assert(BPL16 >= 0x0064 && BPL16 <= 0x007A, "BPL16 in extended range");
    static_assert(BMI16_P15 >= 0x0064 && BMI16_P15 <= 0x007A, "BMI16_P15 in extended range");
    static_assert(BRA16 >= 0x0064 && BRA16 <= 0x007A, "BRA16 in extended range");
    static_assert(BRN16 >= 0x0064 && BRN16 <= 0x007A, "BRN16 in extended range");
    static_assert(BVC16 >= 0x0064 && BVC16 <= 0x007A, "BVC16 in extended range");
    static_assert(BVS16 >= 0x0064 && BVS16 <= 0x007A, "BVS16 in extended range");
    static_assert(BMI16 >= 0x0064 && BMI16 <= 0x007A, "BMI16 in extended range");
    
}} // namespace H8S::ExtBr

// ===== SSoT: SUBX (Subtract with extend, P1.6) =====
namespace H8S { namespace SUBX {
    // Opcodes
    constexpr uint8_t RR_PRIMARY = 0x1E;  // SUBX.B Rr,Rd
    constexpr uint8_t IMM_BASE   = 0xB0;  // SUBX.B #imm8,Rd (B0..B7)
    [[msvc::forceinline]] constexpr inline uint8_t IMM_LAST() { return (uint8_t)(IMM_BASE + 7); }

    // Z flag retention helper: if res!=0 -> Z=0, else keep previous Z
    [[msvc::forceinline]] constexpr inline bool z_after(bool prevZ, uint8_t res) {
        return (res != 0) ? false : prevZ;
    }
}}

static_assert(H8S::SUBX::IMM_BASE == 0xB0, "SUBX imm base must be 0xB0..0xB7");

// Enable this once SHAR SSoT is introduced (e.g., from its header or CMake):
// #define H8S_HAS_SHAR_SSOT 1

#if defined(H8S_HAS_SHAR_SSOT)
#include "h8s2350_shar_contracts.h"
// Protect SUBX(0x1E) vs SHAR primary opcodes
static_assert(H8S::SUBX::RR_PRIMARY != H8S::SHAR::PRIMARY_B &&
              H8S::SUBX::RR_PRIMARY != H8S::SHAR::PRIMARY_W &&
              H8S::SUBX::RR_PRIMARY != H8S::SHAR::PRIMARY_L,
              "Opcode clash: SUBX vs SHAR");
// SHAR has no imm range in this tree; no clash expected with SUBX 0xB0..0xB7
#endif

// ===== SSoT: Shift/Rotate family (P1.8 per H8S/2600) =====
namespace H8S { namespace Shift {
    // Primaries
    constexpr uint8_t SHLL = 0x10;
    constexpr uint8_t SHLR = 0x11;
    constexpr uint8_t ROTL = 0x12;
    constexpr uint8_t ROTR = 0x13;

    // Sub-nibbles (upper nibble of 2nd byte) for .B/.W 1-bit and #2
    // .B: SHLL:0x0/0x4, SHLR:0x0/0x4, ROTL:0x8/0xC, ROTR:0x8/0xC
    // .W: SHLL:0x1/0x5, SHLR:0x1/0x5, ROTL:0x9/0xD, ROTR:0x9/0xD
    // .L: second byte low nibble is 0; high nibble: SHLL:0x2/0x6, SHLR:0x3/0x7, ROTL:0xB/0xF, ROTR:0xB/0xF; 3rd byte is ERd

    // Helpers to detect forms
    constexpr inline bool is_sub_b_1(uint8_t sub, uint8_t prim) {
        if (prim == SHLL || prim == SHLR) return sub == 0x0;
        if (prim == ROTL || prim == ROTR) return sub == 0x8;
        return false;
    }
    constexpr inline bool is_sub_b_2(uint8_t sub, uint8_t prim) {
        if (prim == SHLL || prim == SHLR) return sub == 0x4;
        if (prim == ROTL || prim == ROTR) return sub == 0xC;
        return false;
    }
    constexpr inline bool is_sub_w_1(uint8_t sub, uint8_t prim) {
        if (prim == SHLL || prim == SHLR) return sub == 0x1;
        if (prim == ROTL || prim == ROTR) return sub == 0x9;
        return false;
    }
    constexpr inline bool is_sub_w_2(uint8_t sub, uint8_t prim) {
        if (prim == SHLL || prim == SHLR) return sub == 0x5;
        if (prim == ROTL || prim == ROTR) return sub == 0xD;
        return false;
    }
    constexpr inline bool is_sub_l_1(uint8_t sub, uint8_t prim) {
        // RENDERED H8S/2350 HM Rev 3.00 App A.1 page 769: SHLL.L ERd = 1 0 | 3 0:erd.
        // This said 0x2. SHLL.L and SHLR.L share the sub-nibble 3 and are told apart by
        // the PRIMARY opcode (0x10 vs 0x11), not by the sub. Measured 2026-09-13: `10 32`
        // (SHLL.L ER2) matched nothing and silently did nothing - an endless loop at 0x011E38.
        if (prim == SHLL) return sub == 0x3;
        if (prim == SHLR) return sub == 0x3;
        if (prim == ROTL || prim == ROTR) return sub == 0xB;
        return false;
    }
    constexpr inline bool is_sub_l_2(uint8_t sub, uint8_t prim) {
        if (prim == SHLL) return sub == 0x7;   // SHLL.L #2,ERd = 1 0 | 7 0:erd (rendered p.769)
        if (prim == SHLR) return sub == 0x7;
        if (prim == ROTL || prim == ROTR) return sub == 0xF;
        return false;
    }
}} // namespace H8S::Shift

// ===== SSoT: Rotate-through-carry (ROTXL/ROTXR) and SHAL (P1.9) =====
// Encodings per H8S/2600 manual. These reuse primaries 0x10/0x11/0x12/0x13 with
// distinct sub-nibbles from the no-carry family to avoid opcode overlap.
namespace H8S { namespace ROTX {
    // Primaries: left-group = 0x12, right-group = 0x13
    constexpr uint8_t ROTXL = 0x12;
    constexpr uint8_t ROTXR = 0x13;

    // .B (2B): sub=0x0 (1-bit), 0x4 (#2); lo=rd(0..7)
    constexpr inline bool is_b_1(uint8_t prim, uint8_t sub) {
        return (prim == ROTXL || prim == ROTXR) && sub == 0x0;
    }
    constexpr inline bool is_b_2(uint8_t prim, uint8_t sub) {
        return (prim == ROTXL || prim == ROTXR) && sub == 0x4;
    }
    // .W (2B): sub=0x1 (1-bit), 0x5 (#2); lo=rd(0..7)
    constexpr inline bool is_w_1(uint8_t prim, uint8_t sub) {
        return (prim == ROTXL || prim == ROTXR) && sub == 0x1;
    }
    constexpr inline bool is_w_2(uint8_t prim, uint8_t sub) {
        return (prim == ROTXL || prim == ROTXR) && sub == 0x5;
    }
    // .L (3B): sub=0x3 (1-bit), 0x7 (#2); lo must be 0, 3rd byte = erd(0..7)
    constexpr inline bool is_l_1(uint8_t prim, uint8_t sub, uint8_t lo) {
        (void)prim; return (sub == 0x3) && (lo == 0x0);
    }
    constexpr inline bool is_l_2(uint8_t prim, uint8_t sub, uint8_t lo) {
        (void)prim; return (sub == 0x7) && (lo == 0x0);
    }
} } // namespace H8S::ROTX

namespace H8S { namespace SHAL {
    // SHAL uses primary 0x10, distinct sub-nibbles from SHLL
    constexpr uint8_t PRIMARY = 0x10;

    // .B (2B): sub=0x8 (1-bit), 0xC (#2); lo=rd(0..7)
    constexpr inline bool is_b_1(uint8_t prim, uint8_t sub) {
        return prim == PRIMARY && sub == 0x8;
    }
    constexpr inline bool is_b_2(uint8_t prim, uint8_t sub) {
        return prim == PRIMARY && sub == 0xC;
    }
    // .W (2B): sub=0x9 (1-bit), 0xD (#2)
    constexpr inline bool is_w_1(uint8_t prim, uint8_t sub) {
        return prim == PRIMARY && sub == 0x9;
    }
    constexpr inline bool is_w_2(uint8_t prim, uint8_t sub) {
        return prim == PRIMARY && sub == 0xD;
    }
    // .L (3B): sub=0xB (1-bit), 0xF (#2); lo must be 0, 3rd byte = erd(0..7)
    constexpr inline bool is_l_1(uint8_t prim, uint8_t sub, uint8_t lo) {
        return prim == PRIMARY && sub == 0xB && lo == 0x0;
    }
    constexpr inline bool is_l_2(uint8_t prim, uint8_t sub, uint8_t lo) {
        return prim == PRIMARY && sub == 0xF && lo == 0x0;
    }
} } // namespace H8S::SHAL

// Compile-time uniqueness guards between P1.8 vs P1.9 sub-nibbles
static_assert(H8S::Shift::ROTL == 0x12 && H8S::Shift::ROTR == 0x13, "P1.8 ROTL/ROTR primaries must be 0x12/0x13");
static_assert(H8S::Shift::SHLL == 0x10, "P1.8 SHLL primary must be 0x10");
// Ensure P1.9 uses distinct sub-nibbles from P1.8 for same primaries
static_assert(!H8S::Shift::is_sub_b_1(0x0, H8S::Shift::ROTL), "ROTXL.B sub must not overlap ROTL.B");
static_assert(!H8S::Shift::is_sub_b_1(0x0, H8S::Shift::ROTR), "ROTXR.B sub must not overlap ROTR.B");
static_assert(!H8S::Shift::is_sub_w_1(0x1, H8S::Shift::ROTL), "ROTXL.W sub must not overlap ROTL.W");
static_assert(!H8S::Shift::is_sub_w_1(0x1, H8S::Shift::ROTR), "ROTXR.W sub must not overlap ROTR.W");

// ===== SSoT: ADDX (Add with extend, P1.10) =====
namespace H8S { namespace ADDX {
    // Encodings: reg-reg 0x0E, (rs<<4)|rd; imm8->Rd (0x90|rd), imm8
    constexpr uint8_t RR_PRIMARY = 0x0E;
    constexpr uint8_t IMM_BASE   = 0x90; // 0x90..0x9F (rd in low nibble)

    inline bool is_rr(uint8_t op1) { return op1 == RR_PRIMARY; }
    inline bool is_imm(uint8_t op1) { return (uint8_t)(op1 & 0xF0) == IMM_BASE; }

    inline uint8_t rr_rs(uint8_t op2) { return (op2 >> 4) & 0x0F; }
    inline uint8_t rr_rd(uint8_t op2) { return  op2       & 0x0F; }
    inline uint8_t imm_rd(uint8_t op1) { return op1 & 0x0F; }

    struct CCR { bool H,N,Z,V,C; };
    inline CCR flags_after(uint8_t dst, uint8_t src, uint8_t Cin, uint8_t res) {
        const uint16_t sum = uint16_t(dst) + uint16_t(src) + uint16_t(Cin);
        CCR f{};
        f.H = ((dst & 0x0F) + (src & 0x0F) + Cin) > 0x0F;
        f.C = sum > 0xFF;
        f.N = (res & 0x80) != 0;
        f.Z = (res == 0);
        f.V = ((~(dst ^ src) & (dst ^ res)) & 0x80) != 0;
        return f;
    }
    static_assert(IMM_BASE == 0x90, "ADDX immediate base must be 0x90");
} } // namespace H8S::ADDX

// ===== SSoT: DAA/DAS (P1.11, BCD adjust) =====
namespace H8S { namespace BCDAdj {
    constexpr uint8_t DAA_B_PRIMARY = 0x0F; // DAA.B Rd: 0x0F, 0x0|rd
    constexpr uint8_t DAS_B_PRIMARY = 0x1F; // DAS.B Rd: 0x1F, 0x0|rd

    inline bool is_daa(uint8_t op1, uint8_t op2) { return op1 == DAA_B_PRIMARY && ((op2 & 0xF0u) == 0x00u) && (op2 <= 0x07); }
    inline bool is_das(uint8_t op1, uint8_t op2) { return op1 == DAS_B_PRIMARY && ((op2 & 0xF0u) == 0x00u) && (op2 <= 0x07); }
    inline uint8_t rd(uint8_t op2) { return op2 & 0x07; }
} }

// ===== SSoT: EXTU/EXTS and NEG (P1.11) =====
namespace H8S { namespace ExtNeg {
    constexpr uint8_t PRIMARY = 0x17;
    // EXTU.W Rd: sub=0x5|rd; EXTS.W Rd: sub=0x4|rd
    inline bool is_extu_w(uint8_t b1) { return ((b1 >> 4) & 0x0F) == 0x05; }
    // Manual extract: EXTS.W uses 0xD? sub-nibble
    inline bool is_exts_w(uint8_t b1) { return ((b1 >> 4) & 0x0F) == 0x0D; }
    inline uint8_t rd_w(uint8_t b1) { return b1 & 0x07; }
    // EXTU.L: b1=0x70, b2=erd; EXTS.L: b1=0x60, b2=erd
    inline bool is_extu_l(uint8_t b1) { return b1 == 0x70; }
    // Manual-pinned fix: EXTS.L uses 0xF0 (not 0x60)
    inline bool is_exts_l(uint8_t b1) { return b1 == 0xF0; }
    // Constants for clarity in tests and decode
    namespace EXT {
        constexpr uint8_t EXTS_L_B1 = 0x17;
        constexpr uint8_t EXTS_L_B2 = 0xF0; // manual-pin
    }
    // NEG.B: sub=0x8|rd; NEG.W: sub=0x9|rd; NEG.L: b1=0xB0, b2=erd
    inline bool is_neg_b(uint8_t b1) { return ((b1 >> 4) & 0x0F) == 0x08; }
    inline bool is_neg_w(uint8_t b1) { return ((b1 >> 4) & 0x0F) == 0x09; }
    inline bool is_neg_l(uint8_t b1) { return b1 == 0xB0; }
    inline uint8_t rd_bw(uint8_t b1) { return b1 & 0x07; }
    // CCR helpers: N/Z from result; preserve H/V/C per manual
    struct CCR { bool H,N,Z,V,C; };
    inline void ext_nz_from_result_preserve_hvc(uint32_t result, CCR& out) {
        out.N = (result & 0x80000000u) != 0;
        out.Z = (result == 0);
        // H/V/C unchanged by design
    }
} }

// ===== SSoT: MULXU/MULXS (P1.11) =====
// =====================================================================
// 2026-09-17, BUG81 - THE WHOLE `H8S::Mul` BLOCK IS RETIRED AND DELETED.
//
// It called itself a "manual hard-remap" and it had never been read off a
// rendered page. Four things were wrong at once, and this is the same header
// BUG53 found wrong five ways at once about EXT/NEG:
//
//   MULXU_W_P = 0x52 "0x52, rs, 0x00, erd (4B)"
//       RENDERED PDF page 802 (printed "766"): `MULXU.W Rs,ERd = 5 2 | rs 0:erd`,
//       TWO bytes, no middle byte. A wrong instruction size does not fault -
//       it eats the next instruction (BUG38).
//   MULXS_W_P = 0x57 "0x57, rs, 0x00, erd (4B, signed)"
//       0x57 IS TRAPA - as h8s2350_instructions.h says two lines from the
//       constant that shadowed it - and MULXS has no bare primary at all:
//       `MULXS.B = 01 C0 50 | rs rd`, `MULXS.W = 01 C0 52 | rs 0:erd`, both
//       four bytes, same page. The eighth member of the MOVA.L / MOVU.L /
//       STC VBR / "firmware-specific 0x01 MOV.B" / LDC-@ERn family.
//   LEGACY_MULXU_B/MULXS_B/MULXU_W = 0x11 / 0x12 / 0x15
//       0x15 is XOR.B Rs,Rd - RENDERED page 806, and BUG51 already had to
//       rescue that primary from `NOT_B = 0x15`. 0x11 and 0x12 belong to the
//       shift/rotate group the tree decodes elsewhere; their exact rows are
//       NOT re-read this round, which is why the three are deleted rather
//       than relabelled. They were compiled out anyway
//       (H8S_ENABLE_LEGACY_MULX_DECODE) - a fiction waiting for a #define.
//   mulx_nz_preserve_hvc()
//       RENDERED PDF page 775 (printed "739"): MULXU is `I- H- N- Z- V- C-`.
//       NOT ONE condition code. MULXS writes N and Z only. A helper that
//       writes N and Z for both is wrong for half the family.
//
// The four unsigned rows now live in one encoding-driven decoder and one
// executor body in h8s2350_instructions.cpp; the signed rows live with the
// 0x01 prefix that actually encodes them. Do not reintroduce a contract for
// this group without rendering pages 775, 776, 799 and 802 first.
// =====================================================================

//
// ===== P1.6 SPEC REFINEMENTS - TODO ITEMS =====
// Based on H8S/2600 Software Manual analysis, the following spec-precise 
// implementations need to be added for full compliance:
//
// ~~TODO: SUBX Z-chaining behavior~~ - DONE, AND IT WAS ALREADY DONE.
//   2026-09-18, BUG85: the executor has computed `Z = Z' AND (result == 0)`
//   since before this TODO was written, and RENDERED PDF page 842 (printed
//   "806") confirms the rule exactly - `Z = Z' . /Rm . ...... . /R0` - with
//   note [5] on RENDERED page 792 saying it in words. THE TODO WAS STALE AND
//   POINTED AT THE ONE PART OF SUBX THAT WAS RIGHT, while the register field
//   (four bits, not three), the 0xB8-0xBF half of the immediate row and the
//   `V` formula were all wrong and unmentioned. A worklist that names the
//   wrong item is worse than an empty one: it directs the next reader away.
//   What is still owed on this instruction: nothing known. See BUG85.
//
// TODO: TAS (Test And Set) atomic RMW operation
//   - Must be non-interruptible critical section
//   - Simple "non-interrupt" flag during TAS execution
//   - Prevents IRQ between read-modify-write phases
//
// TODO: STC (Store Control Register) pair-address handling
//   - STC CCR,@ERn with odd address → undefined behavior
//   - Should NOT overwrite adjacent memory on odd addresses  
//   - Implement address alignment check
//
// TODO: BRN (Branch Never) displacement size handling
//   - BRN d:8 vs BRN d:16 have different cycle counts
//   - d:8 format: 2 cycles, d:16 format: 3 cycles (even though never taken)
//   - Update cycle model for proper BRN timing
//
// TODO: IRQ enable/disable delay behavior  
//   - ANDC/ORC EXR operations have 1-instruction delay
//   - IRQ state change takes effect AFTER the next instruction
//   - Add mini-tests for IRQ timing edge cases
//

// Timing constants (using existing definitions from line 22-23)  
constexpr uint32_t H8S_CYC_BRANCH_BASE = 2;

// Legacy static assertions removed - now handled in SSoT namespace above

// How to Run Extended Branch Tests:
//
// ```bash
// # Extended branch family tests (label-based)
// ctest -C Release -L extended_branch
//
// # Memory regression guard  
// ctest -R 0x7A_mem_penalty -C Release
//
// # Full verification with cycle summary
// ctest -C Release --verbose -R "extended_branch|0x7A"
// ```
//
// ===== SSoT ENFORCEMENT GUARDRAILS =====
// 
// MERGE CHECKLIST (Definition of Done):
// [ ] No 0x7C-0x7F literals: everything uses H8S::ExtBr::* constants
// [ ] Single early intercept block in instructions.cpp handles execution  
// [ ] Emulator contains NO branch conditions or displacement calculations
// [ ] Decode-only smoke test PASS + full matrix PASS
// [ ] Release silent, Debug TRACE only under H8S_DEV_TRACE
// [ ] CI grep rules pass (no branch logic contamination in emulator)
//
// CI GREP RULES (must return no matches):
// rg -n "(BLT16|BGE16|BGT16|BLE16|cond_b.*|be_disp16|ext_branch_target)" source/core/h8s2350_emulator.cpp
// rg -n "case 0x00?7[C-F]" source/core/h8s2350_emulator.cpp  
//
// INCLUDE DIRECTION ENFORCEMENT:
// instructions.cpp → contracts.h + emulator.h (✅ allowed)
// emulator.cpp → contracts.h (✅ allowed for constants only)
// emulator.cpp → instructions.h (❌ FORBIDDEN - circular dependency)
//
// Extended Branch Implementation - moved to instructions.cpp to avoid circular dependency

// Big-endian 24-bit helpers (stack frame PC)
static inline uint32_t be24_pack(uint8_t b2, uint8_t b1, uint8_t b0) {
    return (uint32_t(b2) << 16) | (uint32_t(b1) << 8) | uint32_t(b0);
}
static inline void be24_unpack(uint32_t v, uint8_t& b2, uint8_t& b1, uint8_t& b0) {
    b2 = (v >> 16) & 0xFF; b1 = (v >> 8) & 0xFF; b0 = v & 0xFF;
}

// TRAPA/RTE contract (manual):
// - TRAPA always stacks PC+2 (next instruction), frame size = 5 bytes:
//   [SP+4]=PC_MSB, [SP+3]=PC_MID, [SP+2]=PC_LSB, [SP+1]=CCR, [SP+0]=EXR  (big-endian PC24)
// - RTE pops in reverse order: EXR ← CCR ← PC24.
#ifdef H8S_DEV_FAILSAFE
  #include <cstdio>
  #define H8S_DEV_ASSERT(cond, fmt, ...) \
      do { if(!(cond)) std::printf("[H8S-CONTRACT] " fmt "\n", ##__VA_ARGS__); } while(0)
#else
  #define H8S_DEV_ASSERT(cond, fmt, ...) do {} while(0)
#endif

