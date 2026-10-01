#include "h8s2350_instructions.h"
#include "h8s2350_emulator.h"
#include "h8s2350_contracts.h"
#include "h8s2350_debug.h"
#include "diag_panel.h"  // i17.txt: For IRQ diagnostics
#include "crash_dump.h"  // i17.txt: For g_diag extern
#include "branch_forensics.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cinttypes>    // For PRIx64

// === FIX21b: quiet-boot gate for the per-instruction trace flood in this file ===
// --quiet-boot suppresses the [EXECUTE]/[DECODE] per-instruction prints that throttle
// emulation ~1000x (3+ MB/s of log). Statement-level gating (see 'if (!g_h8s_quiet_boot)'
// prefixes); a global printf macro is NOT used because H8S_*_CTX debug macros expand to
// qualified ::printf calls which a function-like macro would corrupt.
bool g_h8s_quiet_boot = false;

// Debug trace only in Debug builds
#ifdef _DEBUG
#define H8S_DEV_TRACE
#endif
// SHIFT/ROT smoke trace: default OFF; define SHIFT_SMOKE_TRACE=1 to enable

namespace MS2000
{
    // Global trace flag (set via setGlobalInsnTrace, gated by --trace-cpu)
    bool g_insn_trace = false;
    void setGlobalInsnTrace(bool v) { g_insn_trace = v; }
    
    // Helper: detect shift/rotate primaries (P1.8/P1.9) and SHAR (P1.7)
    static inline bool is_shiftrot_or_shar_primary(uint8_t p)
    {
        return (p >= 0x10 && p <= 0x13) || (p >= 0x1B && p <= 0x1D);
    }
    // Disable legacy shift/rotate executor/mnemonic tables to avoid opcode collisions
    #ifndef H8S_DISABLE_LEGACY_SHIFT_TABLE
    #define H8S_DISABLE_LEGACY_SHIFT_TABLE 1
    #endif
    // ==== Unified PC Advancement System (boot2.txt + boot3.txt) ====

    // PC24 macro for 24-bit PC masking
    #define PC24(x) ((uint32_t)((x) & 0x00FFFFFF))

    // boot3.txt: 3-bit register extraction macros (H8S standard)
    #define R_HI3(b)   (((b) >> 4) & 0x07)   // Upper nibble 3 LSBs â†’ R/ER index (0-7)
    #define R_LO3(b)   ((b) & 0x07)          // Lower nibble 3 LSBs â†’ R/ER index (0-7)
    #define MODE_HI(b) (((b) >> 7) & 0x01)   // Upper nibble bit7 (mode flag)
    #define MODE_LO(b) (((b) >> 3) & 0x01)   // Lower nibble bit3 (mode flag)

    // Fetch functions that automatically advance PC
    static inline uint8_t  fetch8 (H8S2350Emulator* emulator) {
        uint8_t v = emulator->readByte(emulator->getProgramCounter());
        emulator->setProgramCounter(PC24(emulator->getProgramCounter() + 1));
        return v;
    }

    static inline uint16_t fetch16(H8S2350Emulator* emulator) {
        uint16_t v = emulator->readWord(emulator->getProgramCounter());
        emulator->setProgramCounter(PC24(emulator->getProgramCounter() + 2));
        return v;
    }

    static inline uint32_t fetch24(H8S2350Emulator* emulator) {
        uint32_t b0 = fetch8(emulator), b1 = fetch8(emulator), b2 = fetch8(emulator);
        return (b0 << 16) | (b1 << 8) | b2;
    }

    static inline uint32_t fetch32(H8S2350Emulator* emulator) {
        uint32_t v = emulator->readLong(emulator->getProgramCounter());
        emulator->setProgramCounter(PC24(emulator->getProgramCounter() + 4));
        return v;
    }

    // ==== Helper Functions ====

    // Sprint 2: Simplified dispatch - table replaced with conditional logic
    
    // Get operand size in bytes
    static uint8_t getOperandSize(uint8_t size)
    {
        switch (size) {
            case 0: return 1;  // Byte
            case 1: return 2;  // Word
            case 2: return 4;  // Long
            default: return 1;
        }
    }
    
    // ==== Instruction Decoder Implementation ====
    
    // fw29.txt GPT5 PC-based decode method (memory context access)
    H8S2350Instruction H8S2350InstructionDecoder::decode(H8S2350Emulator& emulator, uint32_t pc)
    {
        H8S2350Instruction instr;
        instr.decoded_pc = pc;
        
        uint8_t primaryOpcode = emulator.readByte(pc);
        if (!emulator.isQuietBoot()) {
            if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-DECODER-START] PC=0x%06X primaryOpcode=0x%02X\n", pc, primaryOpcode);
        }
        instr.decoded_pc = pcMask24(pc);
        
        // inst01.txt FIX: Byte-first fetch - read primary opcode as single byte
        
        // Diagnostic dump for UNKNOWN opcodes to identify next implementation priorities
        bool isUnknown = false;
        
        // Handle 0x58 instruction (POP Rn) - Priority decode before RTE check
        // 0x58 = Bcc d:16 (16-bit displacement conditional branch) - Renesas p.279/280.
        //   58 cc 0  disp16 : cc = condition (0=BRA,1=BRN,2=BHI,3=BLS,4=BCC,5=BCS,6=BNE,7=BEQ,
        //                     8=BVC,9=BVS,A=BPL,B=BMI,C=BGE,D=BLT,E=BGT,F=BLE). All 4 bytes.
        //   The old "POP_RN" mapping was WRONG (POP is 0x6D); 0x58 collided and mis-executed.
        if (primaryOpcode == 0x58) {
            uint8_t b2 = emulator.readByte(pc + 1);
            uint8_t cc = (b2 >> 4) & 0x0F;       // condition code in high nibble
            instr.opcode = 0x58;
            instr.size = 4;                       // 0x58 + cc-byte + 2-byte displacement
            instr.baseCycles = 3;
            instr.source_operand = cc;            // stash condition for the executor
            int16_t disp = (int16_t)(((uint16_t)emulator.readByte(pc + 2) << 8)
                                     | emulator.readByte(pc + 3));
            instr.immediate_value = (uint16_t)disp;
            static const char* ccname[16] = {
                "","","","","","","","",
                "","","","", "", "","",""
            };
            instr.mnemonic = ccname[cc];
            return instr;
        }

        // SHAR (P1.7) - Arithmetic right shift by 1 with sign extend
        // 0x1B = DEC.W / DEC.L / SUBS alcsoport (Renesas H8S/2600 p.115/253/283)
        //   1B 0 0erd  = SUBS #1, ERd
        //   1B 5 rd    = DEC.W #1, Rd      <- a boot-kritikus delay-loop counter dekrement
        //   1B 7 0erd  = DEC.L #1, ERd
        //   1B 8 0erd  = SUBS #2, ERd
        //   1B 9 0erd  = SUBS #4, ERd
        //   1B D rd    = DEC.W #2, Rd
        //   1B F 0erd  = DEC.L #2, ERd
        // The old SHAR.B mapping was WRONG (SHAR is in a different opcode group).
        if (primaryOpcode == 0x1B) {
            uint8_t op2 = emulator.readByte(pc + 1);
            uint8_t hi  = (op2 >> 4) & 0x0F;
            instr.opcode = 0x1B;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = hi;            // sub-op (the high nibble of byte 2)
            instr.destination_operand = op2 & 0x0F;
            switch (hi) {
                case 0x0: instr.mnemonic = "SUBS #1,ERd";  break;
                case 0x5: instr.mnemonic = "DEC.W #1,Rd";  break;
                case 0x7: instr.mnemonic = "DEC.L #1,ERd"; break;
                case 0x8: instr.mnemonic = "SUBS #2,ERd";  break;
                case 0x9: instr.mnemonic = "SUBS #4,ERd";  break;
                case 0xD: instr.mnemonic = "DEC.W #2,Rd";  break;
                case 0xF: instr.mnemonic = "DEC.L #2,ERd"; break;
                default:  instr.mnemonic = "0x1B_UNKNOWN"; break;
            }
            return instr;
        }

        // 0x1C = CMP.B Rs, Rd (Compare Byte Register to Register)
        // 0x1D = CMP.W Rs, Rd (Compare Word Register to Register)
        // Format: op1=0x1C/0x1D, op2=[rn:4][rd:4]
        // Sets N, Z, V, C from Rd - Rs (result discarded)
        if (primaryOpcode == 0x1C || primaryOpcode == 0x1D) {
            uint8_t op2 = emulator.readByte(pc + 1);
            uint8_t rn = (op2 >> 4) & 0x0F;
            uint8_t rd = op2 & 0x0F;
            if (rn < 16 && rd < 16) {
                instr.opcode = primaryOpcode;
                // Instruction length = 2 bytes (size field is instruction length for PC advance)
                // Operand size (0=byte, 1=word) is determined from opcode in executor
                instr.size = 2;
                instr.baseCycles = H8S_CYC_BASE_ALU;
                instr.mnemonic = (primaryOpcode == 0x1C) ? "CMP.B" : "CMP.W";
                // Source=Rs, Destination=Rd for comparison (Rd - Rs)
                instr.source_operand = rn;
                instr.destination_operand = rd;
                return instr;
            } else {
                instr.opcode = primaryOpcode;
                instr.size = 1;
                instr.baseCycles = 1;
                instr.mnemonic = "UNKNOWN";
                return instr;
            }
        }

        // BUG55, 2026-09-16 - primary 0x1F, both rows. RENDERED page 799 (printed "763")
        // for CMP and page 803 (printed "767") for DAS; bit 7 of the second byte selects,
        // the same shape as 0x1A (DEC.B / SUB.L) that BUG-DEFECT-5 fixed:
        //     DAS.B Rd       1 F | 0 rd              2 bytes
        //     CMP.L ERs,ERd  1 F | 1:ers 0:erd       2 bytes
        // Flags for CMP.L, RENDERED page 776: ERd32 - ERs32, result DISCARDED,
        // I- H* N* Z* V* C*, with H set on a borrow out of BIT 27 (footnote [4]).
        // MEASURED: [OPCODE-MISSING] 0x001F at PC 0x00B734, raw bytes [1F 90] - that is
        // CMP.L ER1,ER0, and it is the first fault on the NORMAL boot path.
        if (primaryOpcode == 0x1F) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = 0x1F;
            instr.size = 2;
            instr.baseCycles = 2;
            if (b2 & 0x80) {
                instr.addressing_mode = 0x26;              // marker: CMP.L ERs,ERd
                instr.source_operand      = (b2 >> 4) & 0x07;  // ers
                instr.destination_operand =  b2       & 0x07;  // erd
                instr.mnemonic = "CMP.L ERs,ERd";
            } else {
                instr.destination_operand = b2 & 0x0F;     // rd, the FOUR-bit byte field
                instr.mnemonic = "DAS.B";
            }
            return instr;
        }

        // ===================================================================
        // BUG85, 2026-09-18 - `SUBX Rs,Rd` REJECTED HALF ITS OWN ENCODING.
        //
        // RENDERED PDF page 806 (printed "page 770 of 988"), Table A.2:
        //     SUBX #xx:8,Rd   B   B | rd | IMM     2 bytes
        //     SUBX Rs,Rd      B   1 E | rs rd      2 bytes
        //
        // and `rs`/`rd` are the FOUR-bit 8-bit-register field. The legend on
        // RENDERED PDF page 807 (printed "771") says it in PROSE, not just in
        // the table this file has been citing since Defect 9:
        //     "rs, rd, rn:   Register field (4 BITS specifying an 8-bit or
        //                    16-bit register...)"
        //     "ers, erd, ern, erm:  Register field (3 BITS specifying an
        //                    address register or 32-bit register...)"
        // That single sentence is the whole BUG35 / BUG73 / BUG83 family in
        // one line, and it was one page away the entire time.
        //
        // What stood here: `if ((op2 & 0x88) == 0x00)` - a guard that REFUSES
        // any operand naming an `Rn L` on either side, with `rs`/`rd` masked to
        // three bits and a comment calling them "3-bit reg fields". On refusal
        // it fell through in silence "for robustness".
        //
        // A FILTER THAT CANNOT ACCEPT A LEGAL VALUE IS NOT A FILTER - the
        // BUG59 sentence, and CLAUDE.md's AUDIT-QUEUE has carried this exact
        // site since BUG73 without it ever being shown to fire. It fires now:
        // BUG81-BUG84 opened the path and the run reports
        //     [UNKNOWN-OPCODE] @00DC22: 1E 88 17    (and 0x00DC50, 0x00DC6C)
        // `1E 88` is `SUBX R0L,R0L`. Three sites, all in the 0x00DCxx module -
        // the same module whose `MOV.L ER3,ER2` was BUG84.
        // ===================================================================
        if (primaryOpcode == 0x1E) {
            const uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x1E;
            instr.size = 2;
            instr.baseCycles = H8S_CYC_BASE_ALU;
            instr.addressing_mode = 0x1E;                       // marker: SUBX Rs,Rd
            instr.source_operand      = uint8_t((op2 >> 4) & 0x0F);   // rs, FOUR bits
            instr.destination_operand = uint8_t(op2 & 0x0F);          // rd, FOUR bits
            instr.mnemonic = "SUBX_RR";
            return instr;
        }

        // SHIFT/ROTATE families decode sizing (P1.8, P1.9) â€” ensure size is correct in all builds
        if (primaryOpcode >= 0x10 && primaryOpcode <= 0x13) {
            const uint8_t b1 = emulator.readByte(pc + 1);
            const uint8_t sub = (b1 >> 4) & 0x0F;
            const uint8_t lo  = b1 & 0x0F;
            // BUG113: every row of Table A.2 for 10/11/12/13 (RENDERED p.804-805) is two bytes,
            // second byte = [alt:1 #2:1 size:2][rd] with size 0 = .B, 1 = .W, 3 = .L (then 0:erd).
            // The old classifier had no SHAR rows under 0x11 (8/C, 9/D, B/F), so every SHAR fell
            // through to the legacy decoder and never reached the shift body.
            (void)sub;
            const uint8_t szc = sub & 0x3;
            if (szc != 2 && !(szc == 3 && (lo & 0x08))) {
                instr.opcode = primaryOpcode;
                instr.size = 2;
                instr.baseCycles = H8S_CYC_BASE_ALU;
                return instr;
            }
#ifdef H8S_DISABLE_LEGACY_ALU_HEURISTICS
            // If nothing matched, do not let legacy ALU guess sizes in 0x10..0x1F
            instr.opcode = primaryOpcode;
            instr.size = 1;
            instr.baseCycles = 1;
            instr.mnemonic = "UNKNOWN";
            return instr;
#endif
        }

        // BUG85 - `SUBX #xx:8,Rd = B | rd | IMM`, RENDERED page 806. `rd` is the
        // LOW NIBBLE OF THE FIRST BYTE and it is FOUR bits, so the row spans
        // 0xB0-0xBF - exactly the shape of `ADD.B #xx:8,Rd = 8 rd | IMM` that
        // Defect 11 had to correct. The mask here was `& 0xF8 == 0xB0`, which
        // admits 0xB0-0xB7 only: the EIGHT `Rn L` rows were not decoded at all,
        // and the executor's own dispatch listed the same eight cases.
        if ((primaryOpcode & 0xF0) == 0xB0) {
            instr.opcode = primaryOpcode;
            instr.size = 2;          // opcode + imm8
            instr.baseCycles = H8S_CYC_BASE_ALU;
            instr.addressing_mode = 0x1D;                       // marker: SUBX #xx:8,Rd
            instr.destination_operand = uint8_t(primaryOpcode & 0x0F);   // rd, FOUR bits
            instr.immediate_value = emulator.readByte(pc + 1);
            instr.mnemonic = "SUBX_IMM8";
            return instr;
        }

        // DAA/DAS (P1.11) - DAA.B Rd: 0x0F, 0x0|rd ; DAS.B Rd: 0x1F, 0x0|rd
        if (primaryOpcode == H8S::BCDAdj::DAA_B_PRIMARY || primaryOpcode == H8S::BCDAdj::DAS_B_PRIMARY) {
            uint8_t op2 = emulator.readByte(pc + 1);
            if (((op2 & 0xF0) == 0x00) && (op2 <= 0x07)) {
                instr.opcode = primaryOpcode;
                instr.size = 2;
                instr.baseCycles = H8S_CYC_BASE_ALU;
                instr.destination_operand = H8S::BCDAdj::rd(op2);
                instr.mnemonic = (primaryOpcode == H8S::BCDAdj::DAA_B_PRIMARY) ? "DAA.B" : "DAS.B";
                return instr;
            }
        }

        // BUG53, 2026-09-13 - the WHOLE 0x17 unary group, read off two rendered pages
        // instead of the `H8S::ExtNeg` contract block, which was Tier-3 and wrong in
        // five separate ways at once.
        //
        // RENDERED PDF page 803 (printed "767") - NEG and NOT:
        //     NEG.B Rd    1 7 | 8 rd          NOT.B Rd    1 7 | 0 rd
        //     NEG.W Rd    1 7 | 9 rd          NOT.W Rd    1 7 | 1 rd
        //     NEG.L ERd   1 7 | B 0:erd       NOT.L ERd   1 7 | 3 0:erd
        // RENDERED PDF page 800 (printed "764") - EXTS and EXTU:
        //     EXTS.W Rd   1 7 | D rd          EXTU.W Rd   1 7 | 5 rd
        //     EXTS.L ERd  1 7 | F 0:erd       EXTU.L ERd  1 7 | 7 0:erd
        //
        // TEN rows, ONE primary, ALL TWO BYTES - the 3rd-byte column is empty on both
        // pages for every one of them. What was here:
        //
        //  (1) NO `NOT` AT ALL. Every NOT.B / NOT.W / NOT.L fell past this block into
        //      the `NOT_L` dispatch, which called executeNot() with `instruction.size`
        //      - the instruction LENGTH IN BYTES, 2 - used as the OPERAND size code,
        //      where 2 means longword. So every NOT in this firmware was a 32-bit NOT.
        //      MEASURED at 0x000D00, `17 08` = NOT.B R0L: ER0 went 0x00FFFF00 ->
        //      0xFF0000FF. R0L happened to come out right; E0 and R0H were destroyed.
        //  (2) `is_extu_l(b1) { return b1 == 0x70; }` and the EXTS.L / NEG.L twins tested
        //      the WHOLE second byte, so they matched ER0 ONLY. `17 71` = EXTU.L ER1 -
        //      which the RAM-resident SCI1 driver executes at ram 0x407286 - missed and
        //      fell through to the same 32-bit NOT.
        //  (3) The .L forms were decoded at SIZE 3. They are two bytes. A wrong size is
        //      the worst defect shape in this tree (BUG38): it does not fault, it eats
        //      the next instruction. Only the ER0-and-nothing-else guard in (2) kept
        //      this from firing.
        //  (4) `rd_w`/`rd_bw` masked the register field to THREE bits. It is FOUR
        //      (legend, rendered page 771: 1000-1111 = E0..E7 for words, R0L..R7L for
        //      bytes) - BUG35's defect, still live here.
        //  (5) The contract header's own comments admit it was guessed: "Manual extract:
        //      EXTS.W uses 0xD?" and "Manual-pinned fix: EXTS.L uses 0xF0 (not 0x60)".
        //      A question mark in a source of truth is not a source of truth.
        //
        // The sub-nibble is the whole decode. An unlisted one is REPORTED AND HALTED
        // rather than silently treated as something else.
        if (primaryOpcode == 0x17) {
            const uint8_t b2  = emulator.readByte(pc + 1);
            const uint8_t sub = (b2 >> 4) & 0x0F;
            instr.opcode = 0x17;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.addressing_mode = 0x25;          // marker: 0x17 unary group
            instr.source_operand      = sub;
            instr.destination_operand = b2 & 0x0F; // narrowed to 3 bits by the .L cases
            switch (sub) {
                case 0x0: instr.mnemonic = "NOT.B";  break;
                case 0x1: instr.mnemonic = "NOT.W";  break;
                case 0x3: instr.mnemonic = "NOT.L";  break;
                case 0x5: instr.mnemonic = "EXTU.W"; break;
                case 0x7: instr.mnemonic = "EXTU.L"; break;
                case 0x8: instr.mnemonic = "NEG.B";  break;
                case 0x9: instr.mnemonic = "NEG.W";  break;
                case 0xB: instr.mnemonic = "NEG.L";  break;
                case 0xD: instr.mnemonic = "EXTS.W"; break;
                case 0xF: instr.mnemonic = "EXTS.L"; break;
                default:  instr.mnemonic = "0x17-UNKNOWN"; break;
            }
            return instr;
        }

        // ===================================================================
        // BUG81, 2026-09-17 - 0x50-0x53 IS THE UNSIGNED MULTIPLY/DIVIDE BLOCK,
        // AND EVERY ONE OF THE FOUR ROWS IS **TWO BYTES**.
        //
        // Renesas H8S/2350 HM Rev 3.00, Table A.2, RENDERED PDF page 799
        // (printed "page 763 of 988") for the DIVXU rows and RENDERED PDF page
        // 802 (printed "766") for the MULXU rows:
        //
        //     MULXU.B Rs,Rd    B    5 0 | rs rd        2 bytes
        //     DIVXU.B Rs,Rd    B    5 1 | rs rd        2 bytes
        //     MULXU.W Rs,ERd   W    5 2 | rs 0:erd     2 bytes
        //     DIVXU.W Rs,ERd   W    5 3 | rs 0:erd     2 bytes
        //
        // and the SIGNED forms are 0x01-prefixed, four bytes, on the SAME pages:
        //     MULXS.B  01 C0 50 | rs rd      DIVXS.B  01 D0 51 | rs rd
        //     MULXS.W  01 C0 52 | rs 0:erd   DIVXS.W  01 D0 53 | rs 0:erd
        //
        // WHAT STOOD HERE, from `H8S::Mul` in h8s2350_contracts.h - Tier 3, and
        // wrong three ways at once, the same header that BUG53 found wrong five
        // ways about EXT/NEG:
        //   (1) `MULXU_W_P = 0x52` decoded at SIZE 4 with an invented middle
        //       byte ("Format: 0x52, rs, 0x00, erd"). The page says two bytes.
        //       A WRONG INSTRUCTION SIZE IS THE WORST DEFECT SHAPE IN THIS TREE
        //       (BUG38) - it does not fault, it eats the next instruction.
        //   (2) `MULXS_W_P = 0x57` - and 0x57 IS TRAPA, as this tree's own
        //       header says two lines from the constant. MULXS has no bare
        //       primary at all; it is `01 C0`. The EIGHTH member of the
        //       MOVA.L / MOVU.L / STC VBR / LDC-@ERn family, and it sat AHEAD
        //       of the TRAPA decode, so any `57 xx 00 xx` was stolen from it.
        //   (3) `rs & 0x07` - three bits, where the 16-bit register field is
        //       FOUR (legend, RENDERED page 807, printed 771). BUG35's family.
        //
        // And 0x51/0x53 were not decoded at all: 0x50/0x51/0x52 carried the
        // fictional `CLR_B/CLR_W/CLR_L` constants whose executor ZEROED the
        // destination, and 0x53 halted the boot with [OPCODE-MISSING] - which
        // is how this round started.
        // ===================================================================
        if (primaryOpcode >= 0x50 && primaryOpcode <= 0x53) {
            const uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = primaryOpcode;
            instr.size   = 2;
            instr.addressing_mode = 0x50;              // marker: MULXU/DIVXU block
            instr.source_operand      = uint8_t((op2 >> 4) & 0x0F);   // rs, FOUR bits
            instr.destination_operand = uint8_t(op2 & 0x0F);          // rd / 0:erd
            switch (primaryOpcode) {
                case 0x50: instr.baseCycles = 12; instr.mnemonic = "MULXU.B Rs,Rd";  break;
                case 0x51: instr.baseCycles = 12; instr.mnemonic = "DIVXU.B Rs,Rd";  break;
                case 0x52: instr.baseCycles = 20; instr.mnemonic = "MULXU.W Rs,ERd"; break;
                default:   instr.baseCycles = 20; instr.mnemonic = "DIVXU.W Rs,ERd"; break;
            }
            return instr;
        }

        // ADDX (P1.10) - Reg-Reg form: 0x0E, (rs<<4)|rd  (byte regs: 0..15)
        if (H8S::ADDX::is_rr(primaryOpcode)) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x0E;
            instr.size = 2;
            instr.baseCycles = H8S_CYC_BASE_ALU;
            instr.source_operand = H8S::ADDX::rr_rs(op2); // 0..15
            instr.destination_operand = H8S::ADDX::rr_rd(op2); // 0..15
            instr.mnemonic = "ADDX_RR";
            return instr;
        }
        // ADDX (P1.10) - Imm8->Rd form: (0x90|rd), imm8  (byte regs: 0..15)
        if (H8S::ADDX::is_imm(primaryOpcode)) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = H8S_CYC_BASE_ALU;
            instr.destination_operand = H8S::ADDX::imm_rd(primaryOpcode); // 0..15
            instr.immediate_value = emulator.readByte(pc + 1);
            instr.mnemonic = "ADDX_IMM8";
            return instr;
        }

        // Handle 0x00 prefix extended instructions - Framework for critical patterns
        if (primaryOpcode == 0x00) {
            uint8_t secondByte = emulator.readByte(pc + 1);
            
            // Critical patterns found in diagnostic at PC 0x202-0x216
            if (secondByte == 0x1C) {
                instr.opcode = 0x001C;
                instr.size = 4;  // 0x00 0x1C + 2 operand bytes (tentative)
                instr.baseCycles = 8;
                instr.mnemonic = "EXT_1C";  // Placeholder - needs H8S/2350 manual lookup
                return instr;
            }
            
            if (secondByte == 0x4A) {
                instr.opcode = 0x004A;
                instr.size = 4;  // 0x00 0x4A + 2 operand bytes (tentative)
                instr.baseCycles = 8;
                instr.mnemonic = "EXT_4A";  // Placeholder - needs H8S/2350 manual lookup
                return instr;
            }
            
            if (secondByte == 0x5A) {
                instr.opcode = 0x005A;
                instr.size = 4;  // 0x00 0x5A + 2 operand bytes (tentative)
                instr.baseCycles = 8;
                instr.mnemonic = "EXT_5A";  // Placeholder - needs H8S/2350 manual lookup
                return instr;
            }
            
            if (secondByte == 0x5C) {
                instr.opcode = 0x005C;
                instr.size = 4;  // 0x00 0x5C + 2 operand bytes (tentative)
                instr.baseCycles = 8;
                instr.mnemonic = "EXT_5C";  // Placeholder - needs H8S/2350 manual lookup
                return instr;
            }
            
            // FIX27c: the 0x00-prefix "extended branch" mapping (0x6F-0x7F) is DELETED.
            // It was NOT dead code: it stamped opcode 0x00xx onto desynced streams and
            // fed them to the branch executor. H8S truth: only NOP (0x0000) lives under
            // a 0x00 first byte. Unknown 0x00xx now falls through to the honest
            // unknown-prefix handler below (decode 2 bytes + log).
            
            // Extended ALU operations (0x80-9F with IMM8/IMM16)
            if (secondByte >= 0x80 && secondByte <= 0x9F) {
                instr.opcode = (0x00 << 8) | secondByte;
                
                // 0x80-8F: IMM8 â†’ Rn operations (length=3)
                if (secondByte <= 0x8F) {
                    instr.size = 3;  // 0x00 + op1 + imm8
                    instr.baseCycles = 2;
                    
                    // Decode register from lower 3 bits
                    uint8_t reg = secondByte & 0x07;
                    uint8_t op_family = (secondByte >> 3) & 0x01;  // 0x80-87 vs 0x88-8F
                    
                    if (op_family == 0) {  // 0x80-87
                        switch (reg) {
                            case 0: instr.mnemonic = "AND_IMM8_R0"; break;
                            case 1: instr.mnemonic = "AND_IMM8_R1"; break;
                            case 2: instr.mnemonic = "AND_IMM8_R2"; break;
                            case 3: instr.mnemonic = "AND_IMM8_R3"; break;
                            case 4: instr.mnemonic = "AND_IMM8_R4"; break;
                            case 5: instr.mnemonic = "AND_IMM8_R5"; break;
                            case 6: instr.mnemonic = "AND_IMM8_R6"; break;
                            case 7: instr.mnemonic = "AND_IMM8_R7"; break;
                        }
                    } else {  // 0x88-8F
                        switch (reg) {
                            case 0: instr.mnemonic = "XOR_IMM8_R0"; break;
                            case 1: instr.mnemonic = "XOR_IMM8_R1"; break;
                            case 2: instr.mnemonic = "XOR_IMM8_R2"; break;
                            case 3: instr.mnemonic = "XOR_IMM8_R3"; break;
                            case 4: instr.mnemonic = "XOR_IMM8_R4"; break;
                            case 5: instr.mnemonic = "XOR_IMM8_R5"; break;
                            case 6: instr.mnemonic = "XOR_IMM8_R6"; break;
                            case 7: instr.mnemonic = "XOR_IMM8_R7"; break;
                        }
                    }
                }
                // 0x90-9F: IMM16 / Extended operations (length=4)  
                else {
                    instr.size = 4;  // 0x00 + op1 + imm16
                    instr.baseCycles = 4;
                    instr.mnemonic = "EXT_ALU_" + std::to_string(secondByte - 0x90);
                    isUnknown = true;  // Mark for diagnostic
                }
                return instr;
            }
            
            // Newly discovered patterns from firmware execution
            if (secondByte == 0x20) {  // 00 20 00 pattern
                instr.opcode = 0x0020;
                instr.size = 3;  // 0x00 0x20 0x00 (3 bytes observed)
                instr.baseCycles = 2;  // Conservative estimate
                instr.mnemonic = "EXT_20";
                return instr;
            }
            
            if (secondByte == 0x00) {
                // NOP = 0x0000 (Renesas H8S/2600 p.286, cybemu-confirmed): 2 bytes, PC+=2, no side effects.
                // The old "EXT_00 size=3" was a wrong guess that broke padding regions in firmware.
                instr.opcode = 0x0000;
                instr.size = 2;
                instr.baseCycles = 1;
                instr.mnemonic = "NOP";
                return instr;
            }
            
            // New patterns discovered during execution
            if (secondByte == 0x45) {  // 00 45 F2 pattern
                instr.opcode = 0x0045;
                instr.size = 3;  // 0x00 0x45 0xF2 (3 bytes observed)
                instr.baseCycles = 2;
                instr.mnemonic = "EXT_45";
                return instr;
            }
            
            if (secondByte == 0x5E) {  // 00 5E 00 pattern
                instr.opcode = 0x005E;
                instr.size = 3;  // 0x00 0x5E 0x00 (3 bytes observed)
                instr.baseCycles = 2;
                instr.mnemonic = "EXT_5E";
                return instr;
            }
            
            if (secondByte == 0x0A) {  // 00 0A 18 pattern
                instr.opcode = 0x000A;
                instr.size = 3;  // 0x00 0x0A 0x18 (3 bytes observed)
                instr.baseCycles = 2;
                instr.mnemonic = "EXT_0A";
                return instr;
            }
            
            if (secondByte == 0x0B) {  // 00 0B FA pattern
                instr.opcode = 0x000B;
                instr.size = 3;  // 0x00 0x0B 0xFA (3 bytes observed)
                instr.baseCycles = 2;
                instr.mnemonic = "EXT_0B";
                return instr;
            }
            
            if (secondByte == 0xF9) {  // 00 F9 FF pattern
                instr.opcode = 0x00F9;
                instr.size = 3;  // 0x00 0xF9 0xFF (3 bytes observed)
                instr.baseCycles = 2;
                instr.mnemonic = "EXT_F9";
                return instr;
            }
            
            // Generic 0x00 prefix fallback
            instr.opcode = (0x00 << 8) | secondByte;
            instr.size = 4;  // Assume 4-byte extended instruction
            instr.baseCycles = 8;
            instr.mnemonic = "EXT_UNKNOWN";
            isUnknown = true;
        }

        // Handle 2-byte instructions (RTE, RTS, TRAPA) - Renesas manual format
        
        // --- RTE: 0x56 0x70 ---
        if (primaryOpcode == 0x56 && emulator.readByte(pc + 1) == 0x70) {
            instr.opcode = 0x56;        // H8S2350Opcode::RTE
            instr.size = 2;
            instr.baseCycles = 5;       // 6 if EXR valid (Advanced mode)
            instr.mnemonic = "RTE";
            return instr;
        }
        
        // --- RTS: 0x54 0x70 ---
        if (primaryOpcode == 0x54 && emulator.readByte(pc + 1) == 0x70) {
            instr.opcode = 0x54;        // H8S2350Opcode::RTS
            instr.size = 2;
            instr.baseCycles = 4;       // 5 in Advanced mode
            instr.mnemonic = "RTS";
            return instr;
        }
        
        // --- TRAPA #x: 0x57, 0x00 | (x<<2) ---
        if (primaryOpcode == 0x57) {
            uint8_t b1 = emulator.readByte(pc + 1);
            if ((b1 & 0xF3) == 0x00) {             // Upper nibble 0, lower 2 bits 0
                uint8_t vec = (b1 >> 2) & 0x3;     // 2-bit vector (0-3)
                instr.opcode = 0x57;
                instr.size = 2;
                instr.baseCycles = 7;              // 8 if EXR valid (Advanced mode)
                instr.mnemonic = "TRAPA";
                instr.immediate_value = vec;       // Store vector number
                return instr;
            }
        }

        // SUB.B/W/L register forms:
        //   0x18, (rs << 4) | rd  SUB.B Rs,Rd
        //   0x19, (rs << 4) | rd  SUB.W Rs,Rd
        //   0x1A, (ers << 4) | erd  SUB.L ERs,ERd
        // 0x18/0x19 = SUB.B/SUB.W reg-reg ; 0x1A = SUB.L ERs,ERd  OR  DEC.B Rd  (Renesas p.114/252/283)
        //   1A 0 rd      = DEC.B Rd          (op2 high nibble = 0)
        //   1A 1ers0erd  = SUB.L ERs, ERd    (op2 bit7 = 1)
        if (primaryOpcode == 0x18 || primaryOpcode == 0x19) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = (primaryOpcode == 0x18) ? "SUB.B_R_R" : "SUB.W_R_R";
            return instr;
        }
        if (primaryOpcode == 0x1A) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x1A;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x0F;
            instr.destination_operand = op2 & 0x0F;
            if ((op2 & 0x80) != 0) {
                instr.mnemonic = "SUB.L_ER_ER";   // 1A 1ers0erd
            } else if (((op2 >> 4) & 0x0F) == 0x0) {
                instr.mnemonic = "DEC.B Rd";      // 1A 0 rd
            } else {
                instr.mnemonic = "0x1A_UNKNOWN";
            }
            return instr;
        }
        
        // Renesas H8S/2350 Appendix A: 0x7A is the @aa:32 byte/word MOV prefix.
        // Examples:
        //   7A 0|erd IMM32   MOV.L #xx:32, ERd  (Renesas H8S/2600 manual p.158)
        //   (7A 7|erd IMM32   ADD.L #xx:32, ERd and other ALU-immediate-long forms)
        //   NOTE: byte/word absolute MOV is 0x6A/0x6B, NOT 0x7A. The old comments here
        //   claiming "7A 0r = MOV.B Rs,@aa:32" were WRONG and broke the reset SP-init.
        if (primaryOpcode == 0x7A) {
            instr.opcode = 0x7A;  // Use raw opcode value
            instr.addressing_mode = 0xFF; // 0x7A long-immediate / extended handling
            instr.size = 6;  // 0x7A + sub + imm32(4) = 6 bytes
            instr.baseCycles = 3;
            instr.mnemonic = "0x7A_IMM32";
            return instr;
        }

        // Handle special 0x01 instruction (MOV.B #imm8, Rd) - firmware specific
        if (primaryOpcode == 0x01) {
            // 0x01 is a multi-byte PREFIX in H8S (NOT MOV.B #imm8 - that was a wrong guess).
            // Renesas H8S/2600 manual p.284. Decode by 2nd byte:
            //   01 00 ...  MOV.L extended (@aa, @ERs+, @-ERd, etc.)
            //   01 40 07 IMM   LDC #xx:8, CCR-via-EXR-prefix forms / 01 41 07 IMM = LDC #xx:8, EXR (4 bytes)
            //   01 40/41 6x..  LDC.W/STC.W @ERs/@aa, CCR/EXR
            //   01 80 ...  reserved/SLEEP-class
            //   01 F0 ...  MOV.L/AND.L/OR.L/XOR.L ERs,ERd
            //   01 10 ...  MAC group
            uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = 0x01;
            if ((b2 == 0x40 || b2 == 0x41)) {
                // BUG74, 2026-09-17 - THE PREFIXED IMMEDIATE GROUP IS FOUR SUB-OPS, NOT ONE.
                // Renesas HM Rev 3.00, Table A.2, RENDERED pages 794 (ANDC), 800 (LDC),
                // 803 (ORC), 807 (XORC) - and every one of them is FOUR bytes:
                //     ORC  #xx:8,EXR   0 1 | 4 1 | 0 4 | IMM
                //     XORC #xx:8,EXR   0 1 | 4 1 | 0 5 | IMM
                //     ANDC #xx:8,EXR   0 1 | 4 1 | 0 6 | IMM
                //     LDC  #xx:8,EXR   0 1 | 4 1 | 0 7 | IMM
                // Only 0x07 was decoded here. `01 41 06 F8` = ANDC #0xF8,EXR fell through
                // to the memory branch below, got the right SIZE by luck, and was then
                // executed as a no-op - which is why the interrupt mask never dropped.
                //
                // NOTE: the table has NO `01 40 0x` (CCR) row. The unprefixed forms ARE the
                // CCR ones (`04/05/06/07 | IMM`, two bytes). A `01 40` with one of these
                // sub-ops is not in Appendix A.2; it is decoded so the size is right and the
                // executor reports it rather than inventing a row.
                const uint8_t b3imm = emulator.readByte(pc + 2);
                if (b3imm >= 0x04 && b3imm <= 0x07) {
                    instr.size = 4;
                    instr.baseCycles = 2;
                    instr.mnemonic = (b3imm == 0x04) ? "ORC #xx:8,EXR"
                                   : (b3imm == 0x05) ? "XORC #xx:8,EXR"
                                   : (b3imm == 0x06) ? "ANDC #xx:8,EXR"
                                                     : "LDC #xx:8,EXR";
                    instr.addressing_mode = 0x01;  // marker: 0x01-prefix CCR/EXR immediate
                    instr.source_operand = emulator.readByte(pc + 3);  // the imm8
                    instr.destination_operand = (b2 == 0x41) ? 1 : 0;   // 1=EXR, 0=CCR
                    return instr;
                }
            }
            if ((b2 == 0x40 || b2 == 0x41)) {
                // LDC.W/STC.W @ERs/@aa, CCR/EXR family: 01 4x 6{9,B,D,F} ...
                uint8_t b3 = emulator.readByte(pc + 2);
                int sz = 4;
                if (b3 == 0x6B) { uint8_t b4 = emulator.readByte(pc + 3); sz = (b4 & 0x20) ? 8 : 6; }
                else if (b3 == 0x6F) sz = 6;
                else if (b3 == 0x78) sz = 10;
                else sz = 4; // 69/6D register-indirect forms
                instr.size = sz; instr.baseCycles = 4; instr.mnemonic = "LDC/STC.W(01)";
                instr.addressing_mode = 0x02;
                return instr;
            }
            if (b2 == 0x00) {
                // MOV.L extended group: 01 00 6{9,B,D,F} ...
                uint8_t b3 = emulator.readByte(pc + 2);
                int sz = 4;
                if (b3 == 0x6B) { uint8_t b4 = emulator.readByte(pc + 3); sz = (b4 & 0x20) ? 8 : 6; }
                else if (b3 == 0x6F) sz = 6;
                else if (b3 == 0x78) sz = 10;
                else sz = 4;
                instr.size = sz; instr.baseCycles = 4; instr.mnemonic = "MOV.L_ext(01)";
                instr.addressing_mode = 0x03;
                return instr;
            }
            // STM.L (ERn..ERn+k),@-SP  and  LDM.L @SP+,(ERn..ERn+k)  - Renesas h8s2600 p.284/288.
            //   STM: 01 {10|20|30} 6D F 0ern   (k+1 regs: 10->2, 20->3, 30->4 registers)
            //   LDM: 01 {10|20|30} 6D 7 0ern   (reverse order pop)
            // 4 bytes. b2 high nibble selects count; b3=0x6D; b4 high nibble F=store(STM)/7=load(LDM).
            if ((b2 == 0x10 || b2 == 0x20 || b2 == 0x30) && emulator.readByte(pc + 2) == 0x6D) {
                uint8_t b4 = emulator.readByte(pc + 3);
                bool isStore = ((b4 & 0xF0) == 0xF0);   // F=STM(push), 7=LDM(pop)
                instr.size = 4;
                instr.baseCycles = 6;
                instr.source_operand = (b2 >> 4) & 0x0F;   // count selector: 1->2regs,2->3regs,3->4regs
                // 2026-09-13 - STM AND LDM DO NOT ENCODE THE SAME END OF THE LIST.
                // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED pages 765 and 770:
                //     STM.L (ERn-ERn+k), @-SP    4th byte = F 0:ern       <- the LOWEST register
                //     LDM.L @SP+, (ERn-ERn+k)    4th byte = 7 0:ern+k     <- the HIGHEST register
                // This code took b4&7 as the base for BOTH, so every LDM restored a list
                // starting k registers too high - and with the &7 wrap below that reached ER7,
                // loading the STACK POINTER out of the stack. Measured: `01 20 6D 76` at
                // 0x0042B4 restored ER6/ER7/ER0 instead of ER4-ER6, SP went 0x00FFFBBC ->
                // 0xFFFFFBC8, and the RTE two instructions later popped PC=0x000001.
                {
                    const uint8_t sel  = (b2 >> 4) & 0x0F;      // 1, 2 or 3
                    const uint8_t encr = b4 & 0x07;
                    instr.destination_operand = isStore ? encr             // STM: ern is the base
                                                       : uint8_t(encr - sel);  // LDM: ern+sel is the top
                }
                instr.addressing_mode = isStore ? 0x10 : 0x11;  // marker: 0x10=STM, 0x11=LDM
                instr.mnemonic = isStore ? "STM.L (ERn..),@-SP" : "LDM.L @SP+,(ERn..)";
                return instr;
            }
            // 0x01F0 prefix = the 32-bit LOGICAL group.
            // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED page 767 (OR) and the
            // same rows for AND/XOR:
            //     OR.L  ERs,ERd   0 1  F 0  6 4  0:ers 0:erd
            //     XOR.L ERs,ERd   0 1  F 0  6 5  0:ers 0:erd
            //     AND.L ERs,ERd   0 1  F 0  6 6  0:ers 0:erd
            // FOUR bytes. Flags, RENDERED page 742 (Logical Instructions):
            //     I -  H -  N changes  Z changes  V = 0  C -
            // V is CLEARED, not computed; C and H are untouched.
            //
            // 2026-09-13: `01 F0` used to fall through to the size-2 fallback below, and
            // then into an executor branch inventing a "firmware-specific MOV.B #imm8,Rd".
            // That branch read 0xF0's high nibble as a register index, got 15, and halted
            // the CPU. Measured as HALT-0x11E3C on `01 F0 64 22` = OR.L ER2,ER2.
            if (b2 == 0xF0) {
                const uint8_t b3 = emulator.readByte(pc + 2);
                if (b3 == 0x64 || b3 == 0x65 || b3 == 0x66) {
                    const uint8_t b4 = emulator.readByte(pc + 3);
                    instr.opcode = 0x01;
                    instr.size = 4;
                    instr.baseCycles = 2;
                    instr.addressing_mode = 0x12;                 // marker: 0x01F0 logical group
                    instr.source_operand = b3;                    // 0x64 OR / 0x65 XOR / 0x66 AND
                    instr.destination_operand = b4;               // 0:ers 0:erd
                    instr.mnemonic = (b3 == 0x64) ? "OR.L ERs,ERd"
                                   : (b3 == 0x65) ? "XOR.L ERs,ERd"
                                                  : "AND.L ERs,ERd";
                    return instr;
                }
            }

            // BUG81 - the SIGNED multiply/divide forms are 0x01-prefixed and FOUR bytes.
            // RENDERED PDF page 802 (printed "766") for MULXS, RENDERED PDF page 799
            // (printed "763") for DIVXS:
            //     MULXS.B Rs,Rd    0 1 | C 0 | 5 0 | rs rd
            //     MULXS.W Rs,ERd   0 1 | C 0 | 5 2 | rs 0:erd
            //     DIVXS.B Rs,Rd    0 1 | D 0 | 5 1 | rs rd
            //     DIVXS.W Rs,ERd   0 1 | D 0 | 5 3 | rs 0:erd
            // There is no bare primary for either - the `MULXS_W_P = 0x57` that used to
            // stand in the decoder above was fiction sitting on TRAPA's opcode.
            if (b2 == 0xC0 || b2 == 0xD0) {
                const uint8_t b3 = emulator.readByte(pc + 2);
                const bool wantSub = (b2 == 0xC0) ? (b3 == 0x50 || b3 == 0x52)   // MULXS .B/.W
                                                  : (b3 == 0x51 || b3 == 0x53);  // DIVXS .B/.W
                if (wantSub) {
                    const uint8_t b4 = emulator.readByte(pc + 3);
                    instr.opcode = 0x01;
                    instr.size   = 4;
                    instr.baseCycles = (b3 & 0x02) ? 21 : 13;
                    instr.addressing_mode = 0x51;                       // marker: signed mul/div
                    instr.source_operand      = uint8_t(b3);            // 0x50/0x51/0x52/0x53
                    instr.destination_operand = b4;                     // rs in 7-4, rd/0:erd in 3-0
                    instr.mnemonic = (b3 == 0x50) ? "MULXS.B Rs,Rd"
                                   : (b3 == 0x51) ? "DIVXS.B Rs,Rd"
                                   : (b3 == 0x52) ? "MULXS.W Rs,ERd"
                                                  : "DIVXS.W Rs,ERd";
                    return instr;
                }
            }

            // Fallback: unknown 0x01 prefix form.
            // NOTE: 0x01 is ALWAYS a prefix on this part - there is no two-byte 0x01
            // instruction in Appendix A.1. So reaching here means the decode failed, and
            // the size-2 guess below is a guess. Say so; do not invent an instruction.
            instr.size = 2; instr.baseCycles = 2; instr.mnemonic = "0x01_PREFIX_UNKNOWN";
            if (!g_h8s_quiet_boot) printf("[DEBUG] 0x01 prefix UNKNOWN: 01 %02X at PC 0x%06X (size=2 fallback)\n", b2, pc);
            return instr;
        }

        // ===================================================================
        // BUG84, 2026-09-17 - PRIMARY 0x0F WAS DECODED NOWHERE AND EXECUTED
        // NOWHERE. `MOV.L ERs,ERd` - the plainest longword register move on the
        // part - fell through as a silent PC advance, and every one of them in
        // this firmware simply did not happen. BUG62's shape exactly, on an
        // instruction even more basic than `BSR d:8`.
        //
        // RENDERED PDF page 799 (printed "763") for the DAA row and RENDERED
        // PDF page 802 (printed "766") for the MOV.L row:
        //
        //     DAA Rd          B    0 F | 0 rd          2 bytes
        //     MOV.L ERs,ERd   L    0 F | 1:ers 0:erd   2 bytes
        //
        // BIT 7 OF THE SECOND BYTE SELECTS - the same shape as 0x1A (DEC.B /
        // SUB.L, Defect 5) and 0x1F (DAS.B / CMP.L, BUG55). Flags: MOV.L takes
        // N and Z from the value, CLEARS V, and leaves C and H alone (the MOV.L
        // group rule BUG34 established off the same page); DAA is the decimal
        // adjust the executor already implements.
        //
        // MEASURED, TOOL-PCOFFMAP's ring, the instruction three lines from the
        // derail: `0x00DCB0: 0F B2` = MOV.L ER3,ER2 with ER3 = 0x00000000, and
        // ER2 came out of it still holding 0x00034DBA. That stale ER2 is the
        // index the switch at 0x014608 then shifted and added to its table base
        // - so the call went through a "pointer" read 0x136E8 bytes past the
        // end of the table.
        // ===================================================================
        if (primaryOpcode == 0x0F) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = 0x0F;
            instr.size = 2;
            instr.baseCycles = 2;
            if (b2 & 0x80) {
                instr.addressing_mode = 0x0F;                  // marker: MOV.L ERs,ERd
                instr.source_operand      = uint8_t((b2 >> 4) & 0x07);   // 1:ers
                instr.destination_operand = uint8_t(b2 & 0x07);          // 0:erd
                instr.mnemonic = "MOV.L ERs,ERd";
            } else {
                // `0 rd` - rd is the FOUR-bit 8-BIT register field (legend,
                // RENDERED page 807, printed 771): 0-7 = RnH, 8-15 = RnL.
                instr.addressing_mode = 0x0E;                  // marker: DAA Rd
                instr.destination_operand = uint8_t(b2 & 0x0F);
                instr.mnemonic = "DAA.B";
            }
            return instr;
        }

        // BUG121 (DIFFREF): an invented "MOV.W @(d:16,Rn),Rd" at 0x07, size 4, stood here
        // and pre-empted the BUG38 CCR group below. 0x07 is LDC #xx:8,CCR, TWO bytes
        // (HM REJ09B0330 Appendix A.2, RENDERED p.800). The executor did the LDC right and
        // then the PC ate the next instruction - the exact defect BUG38 describes.

        // Handle 0x00 instruction (NOP) - firmware uses 0x00FF as NOP
        if (primaryOpcode == 0x00) {
            // Check if this is the NOP pattern (0x00FF)
            uint8_t secondByte = emulator.readByte(pc + 1);
            if (secondByte == 0xFF) {
                instr.opcode = 0x00;  // Use raw opcode value for NOP
                instr.size = 2;  // 0x00 + 0xFF
                instr.baseCycles = 2;
                instr.mnemonic = "NOP";
                return instr;
            }
            // If not 0x00FF, fall through to legacy decoder for other 0x00 instructions
        }

        // Handle 0xFF instruction (firmware padding) - treat 0xFFFF as NOP
        if (primaryOpcode == 0xFF) {
            // Check if this is consecutive FF bytes (0xFFFF)
            uint8_t secondByte = emulator.readByte(pc + 1);
            if (secondByte == 0xFF) {
                instr.opcode = 0xFF;  // Use raw opcode value for firmware padding NOP
                instr.size = 2;  // 0xFF + 0xFF
                instr.baseCycles = 2;
                instr.mnemonic = "NOP_FF";
                return instr;
            }
            // If not 0xFFFF, fall through to legacy decoder for other 0xFF instructions
        }

        // Handle 0xF0-0xFF = MOV.B #xx:8, Rd  (0xFr IMM) - Renesas h8s2600 p.154/284.
        // The ENTIRE 0xF0-0xFF range is MOV.B immediate-to-register (2 bytes): low nibble = Rd
        // (0-7 = RnH, 8-15 = RnL), 2nd byte = 8-bit immediate. The legacy decoders below that
        // mapped 0xF0/F4/F6/F7/F8/FC/FE/FF to ROTR/BAND/BOR/BXOR/BTST were WRONG - those bit ops
        // are encoded with 6A/7C-7F prefixes, never with a 0xFr first byte. This single handler
        // runs first and supersedes them all.
        if (primaryOpcode >= 0xF0 && primaryOpcode <= 0xFF) {
            instr.opcode = primaryOpcode;
            instr.size = 2;        // 0xFr + immediate byte
            instr.baseCycles = 2;
            instr.mnemonic = "MOV.B #imm8,Rd";
            return instr;
        }

        // BUG37, 2026-09-13 - the 16-bit logical register-to-register group was not
        // decoded at all. Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED PDF
        // page 806 (printed "page 770 of 988") for XOR, and the same rows for OR/AND:
        //
        //     OR.W  Rs,Rd    6 4   rs rd      2 bytes
        //     XOR.W Rs,Rd    6 5   rs rd      2 bytes
        //     AND.W Rs,Rd    6 6   rs rd      2 bytes
        //
        // `rs` and `rd` are the FOUR-bit 16-bit register field (legend, rendered page
        // 771: 1000-1111 = E0..E7) - BUG35's field. Measured: `[UNKNOWN-OPCODE]
        // @0x004E24: 65 80 73` fired 6,164 times in one run and is `XOR.W E0,R0`.
        // This is the long-standing undiagnosed opcode in the open queue.
        // BUG39/BUG40, 2026-09-13. RENDERED PDF page 798 (printed "page 762") and
        // page 800 (printed "764"):
        //     JSR @ERn      5 D | 0:ern 0     2 bytes
        //     BTST Rn,Rd    6 3 | rn rd       2 bytes
        //     BSR d:8       5 5 | disp        2 bytes
        //     BSR d:16      5 C | 0 0 | disp  4 bytes   <- so 0x5D is NOT BSR
        // Both of these were reaching the executor while the DECODER left the
        // mnemonic "UNKNOWN", which flooded [UNKNOWN-OPCODE] 67 times a run and
        // would have hidden a real unknown behind the noise.
        if (primaryOpcode == 0x5D) {
            instr.opcode = 0x5D;
            instr.size = 2;
            instr.baseCycles = 6;
            instr.mnemonic = "JSR @ERn";
            return instr;
        }
        // BUG82, 2026-09-17 - `JMP @ERn` HAD NO DECODE AND NO EXECUTOR EITHER.
        // RENDERED PDF page 800 (printed "page 764 of 988"), the six rows of the
        // jump block, read off the image row by row:
        //     JMP @ERn     5 9 | 0:ern 0     2 bytes       JSR @ERn     5 D | 0:ern 0
        //     JMP @aa:24   5 A | abs:24      4 bytes       JSR @aa:24   5 E | abs:24
        //     JMP @@aa:8   5 B | abs:8       2 bytes       JSR @@aa:8   5 F | abs:8
        // No condition code is affected (Table A.4, RENDERED page 839: "JMP - - - - -").
        // BUG39 fixed 0x5D and QUOTED THIS VERY ROW FOR 0x59 IN ITS OWN COMMENT while
        // leaving 0x59 undecoded - one half of a block implemented and the other half
        // left, which is the shape this file has now recorded four times (BUG71/73/74/75).
        // MEASURED, and the whole derail is visible in one run: BUG81 let the boot past
        // 0x005E78 into firmware it had never reached, and it immediately hit
        //     [UNKNOWN-OPCODE] @00A062: 59 00 79     = JMP @ER0
        //     [UNKNOWN-OPCODE] @00AD88: 59 30 79     = JMP @ER3
        // five times. Falling through as a silent two-byte PC advance meant the jump did
        // not happen; execution ran into whatever followed and ended at PC 0xF80004 with
        // [MOVL-EXT-UNKNOWN] - a halt with an honest report, on an address the firmware
        // never computed.
        if (primaryOpcode == 0x59) {
            instr.opcode = 0x59;
            instr.size = 2;
            instr.baseCycles = 4;
            instr.addressing_mode = 0x59;                       // marker: JMP @ERn
            instr.destination_operand = uint8_t((emulator.readByte(pc + 1) >> 4) & 0x07);
            instr.mnemonic = "JMP @ERn";
            return instr;
        }
        if (primaryOpcode == 0x63) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = 0x63;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.addressing_mode = 0x21;                 // marker: BTST Rn,Rd
            instr.source_operand      = (b2 >> 4) & 0x0F; // rn - the bit number source
            instr.destination_operand =  b2       & 0x0F; // rd
            instr.mnemonic = "BTST Rn,Rd";
            return instr;
        }

        // BUG51, 2026-09-13 - the BYTE logical register-to-register group. The word
        // group above is BUG37; these are its byte counterparts and they were never
        // decoded at all, so the primary fell through to the enum's fictional
        // XOR_L=0x14 / NOT_B=0x15 / NOT_W=0x16.
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1:
        //     OR.B  Rs,Rd    1 4   rs rd      2 bytes   RENDERED page 803
        //     XOR.B Rs,Rd    1 5   rs rd      2 bytes   RENDERED page 806
        //     AND.B Rs,Rd    1 6   rs rd      2 bytes   RENDERED page 794
        // `rs`/`rd` are the EIGHT-bit register field, four bits each (legend, rendered
        // page 771: 0000-0111 = R0H..R7H, 1000-1111 = R0L..R7L).
        if (primaryOpcode == 0x14 || primaryOpcode == 0x15 || primaryOpcode == 0x16) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.addressing_mode = 0x22;                 // marker: byte logical Rs,Rd
            instr.source_operand      = (b2 >> 4) & 0x0F; // rs
            instr.destination_operand =  b2       & 0x0F; // rd
            instr.mnemonic = (primaryOpcode == 0x14) ? "OR.B Rs,Rd"
                           : (primaryOpcode == 0x15) ? "XOR.B Rs,Rd"
                                                     : "AND.B Rs,Rd";
            return instr;
        }

        if (primaryOpcode == 0x64 || primaryOpcode == 0x65 || primaryOpcode == 0x66) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.addressing_mode = 0x20;                 // marker: word logical Rs,Rd
            instr.source_operand      = (b2 >> 4) & 0x0F; // rs
            instr.destination_operand =  b2       & 0x0F; // rd
            instr.mnemonic = (primaryOpcode == 0x64) ? "OR.W Rs,Rd"
                           : (primaryOpcode == 0x65) ? "XOR.W Rs,Rd"
                                                     : "AND.W Rs,Rd";
            return instr;
        }

        // BUG61 - 0x68 is MOV.B @ERd, and `LDC @ERn,EXR` does not exist. RENDERED page 801:
        //     MOV.B @ERs,Rd   6 8 | 0:ers rd    2 bytes
        //     MOV.B Rs,@ERd   6 8 | 1:erd rs    2 bytes
        // and the TOP of that same page shows every LDC memory form is `0 1 4x`-prefixed.
        if (primaryOpcode == 0x68) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = 0x68;
            instr.size = 2;
            instr.baseCycles = 4;
            instr.mnemonic = (b2 & 0x80) ? "MOV.B Rs,@ERd" : "MOV.B @ERs,Rd";
            return instr;
        }

        // Handle 0x69 family: Pattern-based decoder (MOV.W Rs,@ERd + STC.W EXR/CCR,@ERd)
        if (primaryOpcode == 0x69) {
            if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-DECODER-0x69] PC=0x%06X setting opcode=0x69\n", pc);
            instr.opcode = 0x69;
            instr.size = 2;  // Base size, may be extended based on pattern
            instr.baseCycles = 4;  // Base cycles, varies by pattern
            instr.mnemonic = "0x69_FAMILY";
            return instr;
        }

        if (primaryOpcode == 0x6A) {
            // 0x6A = MOV.B absolute-address group + bit-manipulation @aa group.
            // (Renesas H8S/2600 manual page 154/160: NOT EXTU/unary - that is 0x17.)
            //   6A 0r abs16        MOV.B @aa:16, Rd     4 bytes (load)
            //   6A 2r abs32        MOV.B @aa:32, Rd     6 bytes (load)
            //   6A 8r abs16        MOV.B Rs, @aa:16     4 bytes (store)
            //   6A Ar abs32        MOV.B Rs, @aa:32     6 bytes (store)
            //   6A 1x abs16 <2nd>  bit-instr @aa:16     6 bytes
            //   6A 3x abs32 <2nd>  bit-instr @aa:32     8 bytes
            uint8_t secondByte = emulator.readByte(pc + 1);
            uint8_t operation = (secondByte >> 4) & 0x0F;  // High nibble = sub-op / addressing
            uint8_t reg_idx   = secondByte & 0x0F;         // Low nibble = register (rd/rs)

            instr.opcode = 0x6A;
            instr.source_operand = operation;
            instr.destination_operand = reg_idx;

            switch (operation) {
                case 0x0: instr.size = 4; instr.baseCycles = 3; instr.mnemonic = "MOV.B @aa:16,Rd"; break;
                case 0x2: instr.size = 6; instr.baseCycles = 4; instr.mnemonic = "MOV.B @aa:32,Rd"; break;
                case 0x8: instr.size = 4; instr.baseCycles = 3; instr.mnemonic = "MOV.B Rs,@aa:16"; break;
                case 0xA: instr.size = 6; instr.baseCycles = 4; instr.mnemonic = "MOV.B Rs,@aa:32"; break;
                case 0x1: instr.size = 6; instr.baseCycles = 4; instr.mnemonic = "BIT.B @aa:16"; break;
                case 0x3: instr.size = 8; instr.baseCycles = 5; instr.mnemonic = "BIT.B @aa:32"; break;
                default:  instr.size = 2; instr.baseCycles = 2; instr.mnemonic = "0x6A_UNKNOWN"; break;
            }

            if (!g_h8s_quiet_boot) printf("[DEBUG] 0x6A decoded: 0x%02X 0x%02X (op=0x%X reg=%d) %s size=%d at PC 0x%06X\n",
                   primaryOpcode, secondByte, operation, reg_idx, instr.mnemonic.c_str(), instr.size, pc);
            return instr;
        }

        if (primaryOpcode == 0x6C) { // MOV.B @ERn, Rd
            instr.opcode = 0x6C;
            instr.size = 2;
            instr.baseCycles = 4;
            instr.mnemonic = "MOV.B_INDIRECT_TO_REG";
            return instr;
        }

        if (primaryOpcode == 0x6E) { // MOV.B Rs, @(disp16, ERn)
            instr.opcode = 0x6E;
            instr.size = 4;  // 0x6E + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "MOV.B_REG_TO_DISP_INDIRECT";
            return instr;
        }

        // 0x78 = MOV.B/MOV.W @(d:32, ERn) <-> Rd  (Renesas H8S/2600 p.284/285). 10 BYTES.
        //   78 0ers 0  6A 2rd  disp32  = MOV.B @(d:32,ERs),Rd   (load,  byte)
        //   78 0erd 0  6A Ars  disp32  = MOV.B Rs,@(d:32,ERd)   (store, byte)
        //   78 0ers 0  6B 2rd  disp32  = MOV.W @(d:32,ERs),Rd   (load,  word)
        //   78 0erd 0  6B Ars  disp32  = MOV.W Rs,@(d:32,ERd)   (store, word)
        // The old "MOV.B @(disp16,ERn) size=4" was WRONG and collided with the 0x6F-0x7F
        // extended-branch fallback, mis-executing 0x78 as BVC16.
        if (primaryOpcode == 0x78) {
            instr.opcode = 0x78;
            instr.size = 8;             // 78 + ERn + 2nd-op + reg + 4-byte disp = 8 bytes (Renesas p.284/285)
            instr.baseCycles = 10;
            uint8_t b2 = emulator.readByte(pc + 1);     // 0|ern|0
            uint8_t b3 = emulator.readByte(pc + 2);     // 0x6A (byte) or 0x6B (word)
            uint8_t b4 = emulator.readByte(pc + 3);     // dir|reg : 2x=load, Ax=store
            instr.source_operand = (b2 >> 4) & 0x07;    // ERn (base register)
            instr.destination_operand = b4 & 0x0F;      // data register (rd or rs)
            // stash 2nd-opcode and direction in immediate_value for the executor
            instr.immediate_value = ((uint32_t)b3 << 8) | b4;
            bool isWord = (b3 == 0x6B);
            bool isStore = ((b4 & 0xF0) == 0xA0);
            instr.mnemonic = isStore
                ? (isWord ? "MOV.W Rs,@(d:32,ERd)" : "MOV.B Rs,@(d:32,ERd)")
                : (isWord ? "MOV.W @(d:32,ERs),Rd" : "MOV.B @(d:32,ERs),Rd");
            return instr;
        }

        if (primaryOpcode == 0x79) {
            // 0x79 = immediate-word ALU group (Renesas H8S/2600 p.156/279):
            //   79 0 rd IMM16  MOV.W #xx:16, Rd
            //   79 1 rd IMM16  ADD.W ; 79 2 CMP.W ; 79 3 SUB.W ; 79 4 OR.W ; 79 5 XOR.W ; 79 6 AND.W
            // (Old "MOV.W @(disp16,ERn)" was a WRONG guess.) All 4 bytes.
            uint8_t b2 = emulator.readByte(pc + 1);
            uint8_t sub = (b2 >> 4) & 0x0F;
            uint8_t rd  = b2 & 0x0F;
            instr.opcode = 0x79;
            instr.size = 4;
            instr.baseCycles = 2;
            instr.source_operand = sub;   // ALU sub-op
            // BUG73, 2026-09-16 - THE DECODER THREW AWAY THE BIT THE EXECUTOR WAS FIXED TO READ.
            // Appendix A.2 legend, RENDERED page 771:  16-Bit Register: 0000-0111 -> R0..R7,
            // 1000-1111 -> E0..E7.  `rd` two lines above already takes the FOUR-bit field, and
            // then `& 0x07` collapsed E0..E7 onto R0..R7 on the way into the instruction.
            // BUG35 (2026-09-13) fixed execute0x79Instruction to read `destination_operand & 0x0F`
            // and route through getRegisterValue/setRegisterValue - but it never reached here, so
            // the two halves of one instruction have disagreed in silence ever since. That is
            // BUG39's and BUG58's shape: the decoder and the executor saying different things,
            // with no fault reported by either.
            // MEASURED at 0x006E86, `79 08 00 08` = MOV.W #0x0008,E0, PC ring:
            //     0x006E86  79 08 00 08   ER0=00FF8040
            //     0x006E8A  6C 18 ...     ER0=00FF0008     <- the 8 landed in R0; E0 stayed 0x00FF
            // E0 is the CGRAM upload counter at 0x006E86-0x006E92, so the loop that must send
            // EIGHT bitmap bytes sent 255 - running off the end of the glyph block and pouring
            // the firmware's whole string table into CGRAM. `1B 58` (DEC.W #1,E0) two
            // instructions later was already correct, which is the asymmetry that made it visible.
            instr.destination_operand = rd;   // FOUR bits: bit 3 selects En over Rn

            const char* nm = "0x79_IMMW?";
            switch (sub) {
                case 0x0: nm = "MOV.W #imm16"; break;
                case 0x1: nm = "ADD.W #imm16"; break;
                case 0x2: nm = "CMP.W #imm16"; break;
                case 0x3: nm = "SUB.W #imm16"; break;
                case 0x4: nm = "OR.W #imm16";  break;
                case 0x5: nm = "XOR.W #imm16"; break;
                case 0x6: nm = "AND.W #imm16"; break;
                default: break;
            }
            instr.mnemonic = nm;
            return instr;
        }

        if (primaryOpcode == 0x08) { // ADD.B Rs, Rd (Renesas p.279: 08 rs rd)
            instr.opcode = 0x08;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "ADD.B Rs,Rd";
            return instr;
        }
        if (primaryOpcode == 0x09) { // BUG105: ADD.W Rs,Rd (RENDERED p.794: 09 rs rd)
            instr.opcode = 0x09;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "ADD.W Rs,Rd";
            return instr;
        }

        // 0x0A = INC.B Rd (0A 0 rd) OR ADD.L ERs,ERd (0A 1ers0erd) - Renesas p.66/132/283
        if (primaryOpcode == 0x0A) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x0A;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x0F;
            instr.destination_operand = op2 & 0x0F;
            if ((op2 & 0x80) != 0) {
                instr.mnemonic = "ADD.L ERs,ERd";   // 0A 1ers0erd
            } else if (((op2 >> 4) & 0x0F) == 0x0) {
                instr.mnemonic = "INC.B Rd";          // 0A 0 rd
            } else {
                instr.mnemonic = "0x0A_UNKNOWN";
            }
            return instr;
        }


        // 0x0B = ADDS / INC.W / INC.L group (Renesas p.67/133/134/279)
        //   0B 0 0erd = ADDS #1,ERd   0B 8 0erd = ADDS #2,ERd   0B 9 0erd = ADDS #4,ERd
        //   0B 5 rd   = INC.W #1,Rd   0B D rd   = INC.W #2,Rd
        //   0B 7 0erd = INC.L #1,ERd  0B F 0erd = INC.L #2,ERd
        if (primaryOpcode == 0x0B) {
            uint8_t op2 = emulator.readByte(pc + 1);
            uint8_t hi  = (op2 >> 4) & 0x0F;
            instr.opcode = 0x0B;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = hi;            // sub-op (high nibble of byte 2)
            instr.destination_operand = op2 & 0x0F;
            switch (hi) {
                case 0x0: instr.mnemonic = "ADDS #1,ERd";  break;
                case 0x8: instr.mnemonic = "ADDS #2,ERd";  break;
                case 0x9: instr.mnemonic = "ADDS #4,ERd";  break;
                case 0x5: instr.mnemonic = "INC.W #1,Rd";  break;
                case 0xD: instr.mnemonic = "INC.W #2,Rd";  break;
                case 0x7: instr.mnemonic = "INC.L #1,ERd"; break;
                case 0xF: instr.mnemonic = "INC.L #2,ERd"; break;
                default:  instr.mnemonic = "0x0B_UNKNOWN"; break;
            }
            return instr;
        }
        // MOV.B Rs, Rd = 0C rs rd (Renesas p.284) - 2 bytes
        if (primaryOpcode == 0x0C) {
            instr.opcode = 0x0C;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "MOV.B Rs,Rd";
            return instr;
        }

        // MOV.W Rs, Rd = 0D rs rd (Renesas p.285) - 2 bytes
        if (primaryOpcode == 0x0D) {
            instr.opcode = 0x0D;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "MOV.W Rs,Rd";
            return instr;
        }

        // ===== LEGACY MIGRATION DECODER CASES =====
        // Migrated from legacy switch statement to new system

        // 0x02 is STC CCR,Rd / STC EXR,Rd (two bytes, `02 0 rd` / `02 1 rd`) - NOT
        // "MOV.W #imm16,Rd", which is `79 0 rd | IMM`. It is left alone in this round
        // only because it has not been read off a rendered page yet; it is in the
        // AUDIT-QUEUE and the size-4 claim below is SUSPECT for exactly the reason
        // BUG38 documents. Do not trust it.
        //
        // BUG112, 2026-09-24 - read off the page now: HM Rev 3.00 Appendix A.2, RENDERED
        // p.805 (printed 769): STC.B CCR,Rd = `0 2 | 0 rd`, STC.B EXR,Rd = `0 2 | 1 rd`, TWO
        // bytes. The old size 4 swallowed the next two bytes - measured at 0x00435A, where
        // `02 19 01 41 04 07` (STC EXR,R1L ; ORC #7,EXR) ran as a 4-byte nothing followed by
        // `04 07` = ORC #7,CCR, so the EXR mask was never raised and R1L never saved.
        if (primaryOpcode == 0x02) {
            instr.opcode = 0x02;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "STC.B CCR/EXR,Rd";
            return instr;
        }

        // ===================================================================
        // BUG38, 2026-09-13 - THE WHOLE 0x03-0x07 CCR GROUP WAS FICTION, AND
        // THE SIZES WERE WRONG, WHICH DESYNCS THE INSTRUCTION STREAM.
        //
        // What stood here: 0x04 "MOV.B Rd,@(d:16,Rn)" size 4, 0x05 "MOV.B
        // @(d:16,Rn),Rd" size 4, 0x06 "MOV.W Rd,@(d:16,Rn)" size 4. There are
        // no such encodings. Renesas H8S/2350 HM Rev 3.00, Appendix A.1:
        //
        //   RENDERED PDF page 794 (printed "page 758"):
        //       ANDC #xx:8,CCR   0 6 | IMM        2 bytes
        //       ANDC #xx:8,EXR   0 1 | 4 1 | 0 6 | IMM   (the 4-byte form is PREFIXED)
        //   RENDERED PDF page 803 (printed "page 767"):
        //       ORC  #xx:8,CCR   0 4 | IMM        2 bytes
        //   RENDERED PDF page 807 (printed "page 771"):
        //       XORC #xx:8,CCR   0 5 | IMM        2 bytes
        //   RENDERED PDF page 800 (printed "page 764"):
        //       LDC  #xx:8,CCR   0 7 | IMM        2 bytes
        //       LDC  Rs,CCR      0 3 | 0 rs       2 bytes
        //       LDC  Rs,EXR      0 3 | 1 rs       2 bytes
        //
        // HOW IT WAS CAUGHT, and it is the cleanest measurement of the session.
        // MS2K_SPTRACE over the routine at 0x01FD2E printed a line per
        // instruction boundary - and there was NO LINE for 0x01FD70:
        //
        //     [SPTRACE] pc=0x01FD6E sp=0xFFFBE6
        //     [SPTRACE] pc=0x01FD72 sp=0xFFFBE6
        //
        // The firmware there is `06 FE` = ANDC #0xFE,CCR followed by `6D 76` =
        // POP.W R6. Decoded at size 4, the ANDC SWALLOWED THE POP, so the two
        // bytes its prologue pushed at 0x01FD36 (`6D F6` = PUSH.W R6) were never
        // taken back. The routine's RTS then popped from base-2 and returned to
        // 0xFF0000. THE FIRMWARE'S PROLOGUE AND EPILOGUE ARE EXACTLY SYMMETRIC -
        // 8+8+2 pushed, 8+2+8+8 popped - so the imbalance was ours, again.
        //
        // A WRONG INSTRUCTION SIZE IS THE WORST KIND OF DEFECT IN THIS TREE: it
        // does not fault, it eats the next instruction, and every symptom shows
        // up somewhere else entirely.
        // ===================================================================
        if (primaryOpcode >= 0x04 && primaryOpcode <= 0x07) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.addressing_mode = 0x30;            // marker: CCR immediate group
            instr.source_operand = primaryOpcode;
            instr.mnemonic = (primaryOpcode == 0x04) ? "ORC #xx:8,CCR"
                           : (primaryOpcode == 0x05) ? "XORC #xx:8,CCR"
                           : (primaryOpcode == 0x06) ? "ANDC #xx:8,CCR"
                                                     : "LDC #xx:8,CCR";
            return instr;
        }
        if (primaryOpcode == 0x03) {
            // LDC Rs,CCR = 03 0 rs   /   LDC Rs,EXR = 03 1 rs   (RENDERED page 800).
            // It used to be "MOV.L_IMM32" at SIZE 6 - three instructions' worth of
            // stream eaten every time it appeared.
            instr.opcode = 0x03;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.addressing_mode = 0x31;            // marker: LDC Rs,CCR/EXR
            instr.destination_operand = emulator.readByte(pc + 1);
            instr.mnemonic = "LDC Rs,CCR/EXR";
            return instr;
        }

        // ===== REGISTER OPERATIONS MIGRATION (0x20-0x7F) =====

        // ===================================================================
        // BUG47, 2026-09-13 - 0x20-0x3F IS THE 8-BIT ABSOLUTE MOV.B PAIR, AND
        // IT IS HOW THIS FIRMWARE DRIVES THE LCD.
        //
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED PDF page 801
        // (printed "page 765 of 988"):
        //
        //     MOV.B @aa:8,Rd    B    2 | rd | abs      2 bytes
        //     MOV.B Rs,@aa:8    B    3 | rs | abs      2 bytes
        //
        // `@aa:8` addresses the top page only: the effective address is
        // H'FFFF00 + abs. That is the whole on-chip I/O register page, which is
        // exactly why a firmware bit-banging a port uses this form - it is the
        // shortest and fastest way to touch a port data register.
        //
        // What stood here: 0x20 "ADD #imm8,Rn", 0x21 "ADD Rm,Rn", 0x28
        // "CMP #imm8,Rn", 0x30 "AND #imm8,Rn", 0x38 "SHLL Rn" - five fictions,
        // and the other eleven opcodes of the range were not decoded at all.
        // The real ADD.B #xx:8,Rd is the 0x8n row (BUG32), CMP.B #xx:8,Rd is
        // 0xAn, AND.B #xx:8,Rd is 0xEn - all rendered.
        //
        // WHY IT MATTERS, measured: KOD-A30411 puts the LCD on PORT 2 (pins
        // 72-79 = P20-P27), and P2DR is H'FF61 (RENDERED PDF page 919). The run
        // showed ZERO writes to 0xFFFF61 - which contradicted both Tier-1
        // sources at once. flash.bin settles it: it contains 79 `3n 61` stores
        // and 90 `2n 61` loads. The firmware HAS been driving P2DR all along,
        // through the one addressing form we did not decode.
        // ===================================================================
        if (primaryOpcode >= 0x20 && primaryOpcode <= 0x3F) {
            const bool isStore = (primaryOpcode & 0x10) != 0;   // 0x3n stores, 0x2n loads
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 4;
            instr.addressing_mode = 0x40;                       // marker: MOV.B @aa:8
            instr.source_operand = uint8_t(primaryOpcode & 0x0F);  // rs / rd (BYTE field, 4 bits)
            instr.destination_operand = emulator.readByte(pc + 1); // the abs:8
            instr.mnemonic = isStore ? "MOV.B Rs,@aa:8" : "MOV.B @aa:8,Rd";
            return instr;
        }

        // Handle 0x40 instruction (ADDQ #imm3, Rn)
        // 0x40-0x4F = Bcc d:8 conditional branch group (Renesas p.279/280, confirmed by cybemu).
        //   40=BRA 41=BRN 42=BHI 43=BLS 44=BCC 45=BCS 46=BNE 47=BEQ
        //   48=BVC 49=BVS 4A=BPL 4B=BMI 4C=BGE 4D=BLT 4E=BGT 4F=BLE  (all 2 bytes: 4c disp:8)
        if (primaryOpcode >= 0x40 && primaryOpcode <= 0x4F) {
            static const char* bccNames[16] = {
                "BRA","BRN","BHI","BLS","BCC","BCS","BNE","BEQ",
                "BVC","BVS","BPL","BMI","BGE","BLT","BGT","BLE"
            };
            instr.opcode = primaryOpcode;
            instr.size = 2;            // opcode + signed 8-bit displacement
            instr.baseCycles = 2;
            instr.source_operand = primaryOpcode & 0x0F;   // condition code 0..F
            instr.mnemonic = bccNames[primaryOpcode & 0x0F];
            return instr;
        }

        // Handle 0x46 instruction (MOV Rm, Rn)
        // 0x46 (BNE) handled by the 0x40-0x4F Bcc group decoder above.

        // Handle 0x47 instruction (MOV.L ERm, ERn)
        // 0x47 (BEQ) handled by the 0x40-0x4F Bcc group decoder above.

        // 0x54 is reserved for RTS (0x54 0x70) - handled in 2-byte instructions section above

        // Handle 0x56 instruction (RTE or PUSH Rn)
        if (primaryOpcode == 0x56) {
            instr.opcode = 0x56;
            
            // Distinguish RTE vs PUSH Rn by checking if next byte is valid register
            uint8_t operand = emulator.readByte(pc + 1);  // Read second byte
            uint8_t rn = operand & 0x0F;
            
            if (rn < 8 && (operand & 0xF0) == 0x00) {
                // PUSH Rn (valid register, upper bits zero)
                instr.size = 2;  // 0x56 + operand byte  
                instr.baseCycles = 4;
                instr.mnemonic = "PUSH.B";
            } else {
                // RTE - Return from Exception (single byte)
                instr.size = 1;  // 0x56 only
                instr.baseCycles = 6;  // 5 + 1 for EXR valid
                instr.mnemonic = "RTE";
            }
            return instr;
        }

        // Handle 0x58 instruction (POP Rn) - MOVED TO PRIORITY SECTION ABOVE
        // Removed duplicate check - now handled at top of decode function

        // BUG62, 2026-09-16 - `BSR d:8` WAS NEVER DECODED AND HAD NO EXECUTOR.
        // RENDERED page 798 (printed "762"):  BSR d:8 = 5 5 | disp   2 bytes, signed
        // displacement relative to the NEXT instruction, no condition code affected.
        // The most basic call instruction on the part, and `0x55` appeared nowhere in
        // the decoder or the dispatch - so it fell through as a silent PC advance and
        // every `BSR d:8` in this firmware simply did not happen.
        // MEASURED: `0x407C2E: 55 90` = BSR -> 0x407BC0, the HD44780 NIBBLE WRITER,
        // called once per character from `0x407C26`. With the call missing, the trace
        // shows `0x4C 0x0C 0x4C 0x0C ...` - BSET #6 (RS=1) then BCLR #6 (RS=0) with
        // NOTHING BETWEEN THEM - one pair per character, no E strobe, no data. That is
        // why the string printer ran, ER1 walked "IPL s.p.u [    ]" character by
        // character, and the display stayed empty.
        if (primaryOpcode == 0x55) {
            instr.opcode = 0x55;
            instr.size = 2;
            instr.baseCycles = 4;
            instr.mnemonic = "BSR d:8";
            return instr;
        }

        // Handle 0x5C instruction = BSR d:16 (5C 00 disp16) - Renesas H8S/2600 p.282
        if (primaryOpcode == 0x5C) {
            instr.opcode = 0x5C;
            instr.size = 4;  // 5C 00 disp-hi disp-lo
            instr.baseCycles = 4;
            instr.mnemonic = "BSR d:16";
            return instr;
        }

        // Handle 0x5E instruction = JSR @aa:24 (5E abs24) - Renesas H8S/2600 p.136/284
        // 0x5A = JMP @aa:24 (24-bit absolute jump) - Renesas. 4 bytes: 5A aa-hi aa-mid aa-lo.
        if (primaryOpcode == 0x5A) {
            instr.opcode = 0x5A;
            instr.size = 4;
            instr.baseCycles = 4;
            instr.mnemonic = "JMP @aa:24";
            return instr;
        }

        if (primaryOpcode == 0x5E) {
            instr.opcode = 0x5E;
            instr.size = 4;  // 5E aa-hi aa-mid aa-lo (24-bit absolute address)
            instr.baseCycles = 5;
            instr.mnemonic = "JSR @aa:24";
            return instr;
        }

        // BUG57 - the register-to-register bit group, RENDERED page 798 (printed "762"):
        //     BSET Rn,Rd  6 0 | rn rd     BNOT Rn,Rd  6 1 | rn rd
        //     BCLR Rn,Rd  6 2 | rn rd     BTST Rn,Rd  6 3 | rn rd     (0x63: its own case)
        // The label "ANDC #imm8,CCR" on 0x60 was BUG38's fiction; ANDC is `0 6 | IMM`.
        if (primaryOpcode == 0x60 || primaryOpcode == 0x61 || primaryOpcode == 0x62) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.mnemonic = (primaryOpcode == 0x60) ? "BSET Rn,Rd"
                           : (primaryOpcode == 0x61) ? "BNOT Rn,Rd" : "BCLR Rn,Rd";
            return instr;
        }

        // Handle 0x6B = MOV.W absolute-address group (Renesas H8S/2600 p.285)
        //   6B 0r abs16   MOV.W @aa:16, Rd   4 bytes (load)
        //   6B 2r abs32   MOV.W @aa:32, Rd   6 bytes (load)
        //   6B 8r abs16   MOV.W Rs, @aa:16   4 bytes (store)
        //   6B Ar abs32   MOV.W Rs, @aa:32   6 bytes (store)
        // (The old "LDMS_ERD_AT_ERS" mnemonic was a WRONG guess - no such H8S instruction.)
        if (primaryOpcode == 0x6B) {
            uint8_t secondByte = emulator.readByte(pc + 1);
            uint8_t operation = (secondByte >> 4) & 0x0F;
            uint8_t reg_idx   = secondByte & 0x0F;
            instr.opcode = 0x6B;
            instr.source_operand = operation;
            instr.destination_operand = reg_idx;
            switch (operation) {
                case 0x0: instr.size = 4; instr.baseCycles = 3; instr.mnemonic = "MOV.W @aa:16,Rd"; break;
                case 0x2: instr.size = 6; instr.baseCycles = 4; instr.mnemonic = "MOV.W @aa:32,Rd"; break;
                case 0x8: instr.size = 4; instr.baseCycles = 3; instr.mnemonic = "MOV.W Rs,@aa:16"; break;
                case 0xA: instr.size = 6; instr.baseCycles = 4; instr.mnemonic = "MOV.W Rs,@aa:32"; break;
                default:  instr.size = 2; instr.baseCycles = 2; instr.mnemonic = "0x6B_UNKNOWN"; break;
            }
            if (!g_h8s_quiet_boot) printf("[DEBUG] 0x6B decoded: 0x%02X 0x%02X (op=0x%X reg=%d) %s size=%d at PC 0x%06X\n",
                   primaryOpcode, secondByte, operation, reg_idx, instr.mnemonic.c_str(), instr.size, pc);
            return instr;
        }

        // Handle 0x6D instruction (MOVU.W @ERm+, Rn)
        if (primaryOpcode == 0x6D) {
            instr.opcode = 0x6D;
            instr.size = 2;  // 0x6D + operand byte
            instr.baseCycles = 4;
            instr.mnemonic = "MOVU_W_AT_ERM_INC_RN";
            return instr;
        }

        // 0x6F = MOV.W with a 16-bit displacement. FOUR bytes.
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED pages 765 and 766:
        //     MOV.W @(d:16,ERs),Rd   6 F   0:ers rd   disp    <- bit 7 CLEAR = LOAD
        //     MOV.W Rs,@(d:16,ERd)   6 F   1:erd rs   disp    <- bit 7 SET   = STORE
        //
        // 2026-09-13: this used to decode as "MOVU.L @ERm+,ERn", size 2. MOVU is an
        // H8SX instruction and has **ZERO hits** in this manual - the same test that
        // condemned the MOVA.L fiction in FIX27c, and the same verdict. The wrong
        // size is what desynced the stream: measured as a [SIZE-ERROR] flood at
        // PC=0x011D1A, two bytes into the `6F 30 00 80` at 0x011D18.
        if (primaryOpcode == 0x6F) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x6F;
            instr.size = 4;                             // opcode + operand + disp:16
            instr.baseCycles = 3;
            instr.source_operand = (op2 >> 4) & 0x0F;   // bits 6-4 = address reg, bit 7 = direction
            instr.destination_operand = op2 & 0x0F;     // bits 3-0 = data register
            instr.mnemonic = ((op2 & 0x80) ? "MOV.W Rs,@(d:16,ERd)" : "MOV.W @(d:16,ERs),Rd");
            return instr;
        }

        // FIX27c: 0x70 = BSET #xx:3, Rd (Renesas HM: "BSET 7 0 0IMM rd", 2 bytes).
        // The old "MOVA.L" mapping was FICTION - MOVA is H8SX-only, ZERO hits in the
        // H8S/2350 manual. The wrong 4-byte size desynced the stream at 0x44D6.
        if (primaryOpcode == 0x70) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x70;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x07;   // bit number
            instr.destination_operand = op2 & 0x0F;     // register (0-7=RnH, 8-15=RnL)
            instr.mnemonic = "BSET #imm3,Rd";
            return instr;
        }

        // FIX27c: 0x71 = BNOT #xx:3, Rd (Renesas HM: "BNOT 7 1 0IMM rd", 2 bytes).
        if (primaryOpcode == 0x71) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x71;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x07;
            instr.destination_operand = op2 & 0x0F;
            instr.mnemonic = "BNOT #imm3,Rd";
            return instr;
        }

        // 0x72 = BCLR #xx:3, Rd (register-direct bit clear) - Renesas p.79/280.
        //   72 0 IMM rd : IMM = bit number (high nibble), rd = register (low nibble, 0-7=RnH, 8-15=RnL).
        //   The old MOVA_L mapping was WRONG (4-byte) and collided with the 0x6F-0x7F branch fallback.
        if (primaryOpcode == 0x72) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x72;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x07;   // bit number
            instr.destination_operand = op2 & 0x0F;     // register
            instr.mnemonic = "BCLR #imm3,Rd";
            return instr;
        }

        // 0x67 = BST / BIST #xx:3, Rd  (register-direct bit STORE from the carry) -
        // the companion of the 0x77 BLD/BILD below.
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED page 762:
        //     BST  #xx:3,Rd    6 7   0:IMM: rd
        //     BIST #xx:3,Rd    6 7   1:IMM: rd
        // Operation and flags, RENDERED page 748:
        //     BST    C -> (#xx:3 of Rd8)        BIST   NOT C -> (#xx:3 of Rd8)
        //     I - H - N - Z - V - C -   NO CONDITION CODE IS AFFECTED AT ALL.
        // 2 bytes. Found as [OPCODE-MISSING] 0x0067 at PC 0x00E7C2, bytes [67 08]
        // = BST #0,R0L, reached only after the byte-register-half fix.
        if (primaryOpcode == 0x67) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x67;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x0F;   // bits 2..0 = bit number, bit 3 = invert (BIST)
            instr.destination_operand = op2 & 0x0F;     // register (0-7=RnH, 8-15=RnL, rendered p.771)
            instr.mnemonic = ((op2 & 0x80) ? "BIST #imm3,Rd" : "BST #imm3,Rd");
            return instr;
        }

        // 0x77 = BLD / BILD #xx:3, Rd  (register-direct bit LOAD into the carry).
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED page 761:
        //     BLD  #xx:3,Rd    7 7   0:IMM: rd
        //     BILD #xx:3,Rd    7 7   1:IMM: rd
        // so BIT 7 OF THE SECOND BYTE selects the inverting form - it is not part of
        // the bit number. Kept in source_operand bit 3 rather than thrown away.
        // Operation and flags, RENDERED page 748:
        //     BLD    (#xx:3 of Rd8) -> C        BILD   NOT(#xx:3 of Rd8) -> C
        //     I - H - N - Z - V -   C changes.  ONLY the carry.
        // 2 bytes, 1 state (advanced).
        //
        // Found 2026-09-13 as [OPCODE-MISSING] 0x0077 at PC 0x004DAA, raw bytes
        // [77 22] = BLD #2,R2H - reached only on the second TGI2A pass, which the
        // machine could not get to until the exception frame was fixed.
        if (primaryOpcode == 0x77) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x77;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x0F;   // bits 2..0 = bit number, bit 3 = invert (BILD)
            instr.destination_operand = op2 & 0x0F;     // register (0-7=RnH, 8-15=RnL)
            instr.mnemonic = ((op2 & 0x80) ? "BILD #imm3,Rd" : "BLD #imm3,Rd");
            return instr;
        }

        // FIX27c: 0x73 = BTST #xx:3, Rd (Renesas HM, 2 bytes). Old "MOVA.L" = fiction.
        if (primaryOpcode == 0x73) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = 0x73;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x07;
            instr.destination_operand = op2 & 0x0F;
            instr.mnemonic = "BTST #imm3,Rd";
            return instr;
        }

        // BUG117, 2026-09-25 - 0x74 / 0x75 / 0x76 are BOR/BIOR, BXOR/BIXOR and BAND/BIAND
        // #xx:3,Rd. HM Rev 3.00 Appendix A.2, RENDERED pp.798/799/794 (encoding) and
        // pp.796/797 (BIOR/BIAND/BIXOR): `7 4 | i:IMM rd`, `7 5 | i:IMM rd`, `7 6 | i:IMM rd`,
        // bit 7 of the 2nd byte selecting the inverting form. Operation and CCR, software
        // manual REJ09B0139 RENDERED p.104 (printed 88, BXOR): C (op) bit -> C, ONLY C.
        // What stood here were "LDC @ERn,VBR", "LDC ERn,VBR" and "MOVCO.L ERn,@ERm" -
        // there is no VBR and no MOVCO in this instruction set - and none was dispatched,
        // so 0x75 printed [HONEST-UNIMPL] and ran as a 2-byte NOP. The demo sequencer hit it
        // 1134 times at 0xCBB4 in 45 s (_gui_demo4.log).
        if (primaryOpcode == 0x74 || primaryOpcode == 0x75 || primaryOpcode == 0x76) {
            uint8_t op2 = emulator.readByte(pc + 1);
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.source_operand = (op2 >> 4) & 0x0F;   // bits 2..0 = bit number, bit 3 = invert
            instr.destination_operand = op2 & 0x0F;     // 0-7 = RnH, 8-15 = RnL
            static const char* nm[3][2] = {{"BOR #imm3,Rd","BIOR #imm3,Rd"},
                                           {"BXOR #imm3,Rd","BIXOR #imm3,Rd"},
                                           {"BAND #imm3,Rd","BIAND #imm3,Rd"}};
            instr.mnemonic = nm[primaryOpcode - 0x74][(op2 & 0x80) ? 1 : 0];
            return instr;
        }

        // Handle 0x77 instruction (BOR #imm3, @ERn)
        if (primaryOpcode == 0x77) {
            instr.opcode = 0x77;
            instr.size = 2;  // 0x77 + operand byte
            instr.baseCycles = 6;
            instr.mnemonic = "BOR_IMM3_AT_ERN";
            return instr;
        }

        // BUG52, 2026-09-13 - 0x7B is EEPMOV, and NOTHING ELSE.
        // What stood here was "STC VBR, ERn" at size 2. **There is no STC VBR in this
        // instruction set**: a sweep of Appendix A.1 finds sixteen STC rows and every one
        // of them is CCR or EXR - `STC CCR,Rd = 0 2 | 0 rd`, `STC EXR,Rd = 0 2 | 1 rd`,
        // and the memory forms are 0x01-prefixed. It is the same family as MOVA.L /
        // MOVU.L / "firmware-specific 0x01 MOV.B" / EXT_W / NOT_B.
        // RENDERED PDF page 799 (printed "page 763 of 988"), the only two 7B rows:
        //     EEPMOV.B   7 B | 5 C | 5 9 | 8 F      4 bytes, no operand fields
        //     EEPMOV.W   7 B | D 4 | 5 9 | 8 F      4 bytes, no operand fields
        // The wrong SIZE is the dangerous half: at size 2 the decoder hands back the
        // `5 9 | 8 F` tail as two more instructions. That is the shape BUG38 cost a
        // session to find.
        if (primaryOpcode == 0x7B) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            instr.opcode = 0x7B;
            instr.size = 4;
            instr.baseCycles = 4;
            instr.addressing_mode = (b2 == 0xD4) ? 0x23 : 0x24;  // 0x23 = .W, 0x24 = .B
            instr.mnemonic = (b2 == 0xD4) ? "EEPMOV.W" : "EEPMOV.B";
            return instr;
        }

        // BUG56, 2026-09-16 - the 0x7C-0x7F MEMORY bit-manipulation group.
        // RENDERED PDF page 799 (printed "763") and its neighbours. EVERY row in this
        // group is FOUR BYTES, and the destination differs only in the second byte:
        //
        //     7 C | 0:erd 0 | <op> <operand>     @ERd   - the test/load half
        //     7 D | 0:erd 0 | <op> <operand>     @ERd   - the modify half
        //     7 E |   abs   | <op> <operand>     @aa:8  - EA = H'FFFF00 + abs
        //     7 F |   abs   | <op> <operand>     @aa:8
        //
        // The 3rd and 4th bytes are LITERALLY the register-direct encoding with the `rd`
        // nibble zeroed - 60/61/62/63 for the Rn forms, 67 for BST/BIST, 70-77 for the
        // #xx:3 forms - which is the same pattern the `6A 18` (@aa:16) and `6A 38`
        // (@aa:32) prefixes already use in this tree.
        //
        // WHAT WAS HERE: sizes of 3 and 2, a claim that 0x7E/0x7F address `@aa:32` (they
        // are `@aa:8`), four "BITMANIP_*" mnemonics, and NO EXECUTOR AT ALL.
        // MEASURED on the normal boot path: `[SIZE-ERROR] PC=0x011EF8 opcode=0x7C
        // invalid size=3` - 1,962,480 lines in the first two million of the log. The
        // size validator caught it only because 3 is not in {2,4,6,8,10}; had the
        // fiction guessed an even number it would have desynced the stream silently,
        // which is BUG38's shape and the worst defect shape in this tree.
        if (primaryOpcode >= 0x7C && primaryOpcode <= 0x7F) {
            const uint8_t b2 = emulator.readByte(pc + 1);
            const uint8_t b3 = emulator.readByte(pc + 2);
            instr.opcode = primaryOpcode;
            instr.size = 4;                       // every row, without exception
            instr.baseCycles = 6;
            instr.addressing_mode = 0x27;         // marker: memory bit-manipulation
            instr.source_operand      = b2;       // 0:erd 0  or  abs:8
            instr.destination_operand = b3;       // the operation byte
            instr.mnemonic = (primaryOpcode & 1) ? "BITOP @mem (modify)"
                                                 : "BITOP @mem (test)";
            return instr;
        }


        // Handle 0x78 instruction (BXOR #imm3, @ERn)
        if (primaryOpcode == 0x78) {
            instr.opcode = 0x78;
            instr.size = 2;  // 0x78 + operand byte
            instr.baseCycles = 6;
            instr.mnemonic = "BXOR_IMM3_AT_ERN";
            return instr;
        }

        // Handle 0x79 instruction (BCLR #imm3, @ERn)
        // NOTE: second 0x79 decoder (BCLR_IMM3_AT_ERN) removed - 0x79 is the
        //   immediate-word ALU group, decoded above (Renesas p.156/279).

        // 0x7F duplikĂˇlt dekĂłder eltĂˇvolĂ­tva - most csoportos kezelĂ©s lesz

        // Handle 0x80 instruction (MOV.B #imm8, Rn)
        if (primaryOpcode == 0x80) {
            instr.opcode = 0x80;
            instr.size = 2;  // 0x80 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "MOV_B_IMM8_RN";
            return instr;
        }

        // 0xA0-0xAF = CMP.B #xx:8, Rd (Renesas p.107/299, cybemu cross-check). All 2 bytes.
        //   The old BRA_AT_LABEL/JSR_AT_AA (4-byte) mapping was WRONG and corrupted 0x445.
        if (primaryOpcode >= 0xA0 && primaryOpcode <= 0xAF) {
            instr.opcode = primaryOpcode;
            instr.size = 2;             // opcode + 8-bit immediate
            instr.baseCycles = 1;
            instr.mnemonic = "CMP.B #imm8,Rd";
            return instr;
        }



        // Handle 0xE0-0xEF = AND.B #xx:8, Rd  (E rd IMM) - Renesas H8S/2600 p.279/289
        // Low nibble rd: 0-7 = R0H-R7H, 8-15 = R0L-R7L. 2nd byte = 8-bit immediate.
        if (primaryOpcode >= 0xE0 && primaryOpcode <= 0xEF) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 2;
            instr.mnemonic = "AND.B #imm8,Rd";
            instr.addressing_mode = 0xE0;  // marker for AND.B immediate
            return instr;
        }

        // 0xC0-0xCF = OR.B #xx:8, Rd (Renesas h8s2600 p.178). 2 bytes; low nibble = Rd (0-7=RnH,8-15=RnL).
        if (primaryOpcode >= 0xC0 && primaryOpcode <= 0xCF) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "OR.B #imm8,Rd";
            return instr;
        }

        // 0xD0-0xDF = XOR.B #xx:8, Rd (Renesas h8s2600 p.same logical group). 2 bytes.
        if (primaryOpcode >= 0xD0 && primaryOpcode <= 0xDF) {
            instr.opcode = primaryOpcode;
            instr.size = 2;
            instr.baseCycles = 1;
            instr.mnemonic = "XOR.B #imm8,Rd";
            return instr;
        }

        // Handle 0xE8 instruction (XOR.B Rn, Rm)
        if (primaryOpcode == 0xE8) {
            instr.opcode = 0xE8;
            instr.size = 2;  // 0xE8 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "XOR_B_RN_RM";
            return instr;
        }

        // Handle 0xE9 instruction (XOR.W Rn, Rm)
        if (primaryOpcode == 0xE9) {
            instr.opcode = 0xE9;
            instr.size = 2;  // 0xE9 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "XOR_W_RN_RM";
            return instr;
        }

        // Handle 0xF0 instruction (ROTR ERn)
        if (primaryOpcode == 0xF0) {
            instr.opcode = 0xF0;
            instr.size = 2;  // 0xF0 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "ROTR_ERN";
            return instr;
        }

        // Handle 0xF6 instruction (BAND #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF6) {
            instr.opcode = 0xF6;
            instr.size = 4;  // 0xF6 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BAND_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xF7 instruction (BOR #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF7) {
            instr.opcode = 0xF7;
            instr.size = 4;  // 0xF7 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BOR_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xF8 instruction (BXOR #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF8) {
            instr.opcode = 0xF8;
            instr.size = 4;  // 0xF8 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BXOR_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xFC instruction (BTST #imm3, Rn)
        if (primaryOpcode == 0xFC) {
            instr.opcode = 0xFC;
            instr.size = 2;  // 0xFC + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "BTST_IMM3_RN";
            return instr;
        }

        // Handle 0xFF instruction (BXOR #imm3, Rn)
        if (primaryOpcode == 0xFF) {
            instr.opcode = 0xFF;
            instr.size = 2;  // 0xFF + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "BXOR_IMM3_RN";
            return instr;
        }

        // ===== MEMORY OPERATIONS MIGRATION (0x80-0xFF) CONTINUATION =====

        // Handle 0x80-0x8F range (MOV.B #imm8, Rn)
        if (primaryOpcode >= 0x80 && primaryOpcode <= 0x8F) {
            instr.opcode = primaryOpcode;
            instr.size = 2;  // opcode + immediate byte
            instr.baseCycles = 2;
            instr.mnemonic = "MOV_B_IMM8_RN";
            return instr;
        }





        // Handle 0xE8 instruction (XOR.B Rn, Rm)
        if (primaryOpcode == 0xE8) {
            instr.opcode = 0xE8;
            instr.size = 2;  // 0xE8 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "XOR_B_RN_RM";
            return instr;
        }

        // Handle 0xE9 instruction (XOR.W Rn, Rm)
        if (primaryOpcode == 0xE9) {
            instr.opcode = 0xE9;
            instr.size = 2;  // 0xE9 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "XOR_W_RN_RM";
            return instr;
        }

        // Handle 0xF0 instruction (ROTR ERn)
        if (primaryOpcode == 0xF0) {
            instr.opcode = 0xF0;
            instr.size = 2;  // 0xF0 + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "ROTR_ERN";
            return instr;
        }

        // Handle 0xF4 instruction (BCLR #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF4) {
            instr.opcode = 0xF4;
            instr.size = 4;  // 0xF4 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BCLR_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xF6 instruction (BAND #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF6) {
            instr.opcode = 0xF6;
            instr.size = 4;  // 0xF6 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BAND_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xF7 instruction (BOR #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF7) {
            instr.opcode = 0xF7;
            instr.size = 4;  // 0xF7 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BOR_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xF8 instruction (BXOR #imm3, @(disp, ERn))
        if (primaryOpcode == 0xF8) {
            instr.opcode = 0xF8;
            instr.size = 4;  // 0xF8 + operand + 2 bytes displacement
            instr.baseCycles = 6;
            instr.mnemonic = "BXOR_IMM3_AT_DISP_ERN";
            return instr;
        }

        // Handle 0xFC instruction (BTST #imm3, Rn)
        if (primaryOpcode == 0xFC) {
            instr.opcode = 0xFC;
            instr.size = 2;  // 0xFC + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "BTST_IMM3_RN";
            return instr;
        }

        // Handle 0xFE instruction (BXOR #imm3, Rn) - the missing one we saw in firmware
        if (primaryOpcode == 0xFE) {
            instr.opcode = 0xFE;
            instr.size = 2;  // 0xFE + operand byte
            instr.baseCycles = 2;
            instr.mnemonic = "BXOR_IMM3_RN";
            return instr;
        }
        
        // For multi-byte instructions that are not handled above, construct word from bytes (big-endian)
        uint8_t secondByte = emulator.readByte(pc + 1);
        uint16_t firstWord = (primaryOpcode << 8) | secondByte;

        // DEBUG: Log what we're decoding for troubleshooting
        if (pc >= 0x810 && pc <= 0x820) {
            if (!g_h8s_quiet_boot) printf("[DEBUG] Decoding at PC 0x%06X: 0x%02X 0x%02X -> word 0x%04X\n",
                   pc, primaryOpcode, secondByte, firstWord);
        }

        // Use legacy decoder for complex multi-byte instructions
        instr = decode(firstWord);

        // Enhanced size calculation with memory context
        instr.size = calculateInstructionSize(emulator, pc, firstWord);

        // Set base cycles based on instruction complexity
        instr.baseCycles = calculateBaseCycles(instr.opcode, instr.addressing_mode);
        
        // Diagnostic dump for UNKNOWN opcodes to prioritize next implementation
        if (instr.mnemonic == "UNKNOWN" || instr.mnemonic.find("UNKNOWN") != std::string::npos) {
            const uint8_t b0 = emulator.readByte(pc);
            const uint8_t b1 = emulator.readByte(pc + 1);  
            const uint8_t b2 = emulator.readByte(pc + 2);
            printf("[UNKNOWN-OPCODE] @%06X: %02X %02X %02X (decoded as size=%d cycles=%d)\n", 
                   pc & 0x00FFFFFF, b0, b1, b2, instr.size, instr.baseCycles);
        }
        
        return instr;
    }
    
    // Legacy decoder (compatibility)
    H8S2350Instruction H8S2350InstructionDecoder::decode(uint16_t instruction)
    {
        H8S2350Instruction instr;
        
        instr.opcode = extractOpcode(instruction);
        instr.addressing_mode = extractAddressingMode(instruction);
        instr.source_operand = extractSourceOperand(instruction);
        instr.destination_operand = extractDestinationOperand(instruction);
        instr.mnemonic = getMnemonic(instr);
        
        // Determine instruction size based on opcode
        if (instr.opcode >= H8S2350Opcode::MOV_B && instr.opcode <= H8S2350Opcode::NOT_L) {
            // Basic instructions with size in opcode
            if (instr.opcode % 3 == 0) instr.size = 0; // Byte
            else if (instr.opcode % 3 == 1) instr.size = 1; // Word
            else instr.size = 2; // Long
        } else if (instr.opcode >= H8S2350Opcode::SHAL_B && instr.opcode <= H8S2350Opcode::ROTXR_L) {
            // Shift/rotate instructions
            if (instr.opcode % 3 == 0) instr.size = 0; // Byte
            else if (instr.opcode % 3 == 1) instr.size = 1; // Word
            else instr.size = 2; // Long
        } else if (instr.opcode >= H8S2350Opcode::PUSH_B && instr.opcode <= H8S2350Opcode::POP_L) {
            // Stack instructions
            if (instr.opcode % 3 == 0) instr.size = 0; // Byte
            else if (instr.opcode % 3 == 1) instr.size = 1; // Word
            else instr.size = 2; // Long
        } else if (instr.opcode >= H8S2350Opcode::INC_B && instr.opcode <= H8S2350Opcode::DEC_L) {
            // Increment/decrement instructions
            if (instr.opcode % 3 == 0) instr.size = 0; // Byte
            else if (instr.opcode % 3 == 1) instr.size = 1; // Word
            else instr.size = 2; // Long
        // BUG81: the "clear/test instructions" arm that stood here spanned 0x50-0x55
        // and assigned an operand SIZE by `opcode % 3` - a rule invented to fit the
        // retired CLR/TST constants. That range is MULXU.B / DIVXU.B / MULXU.W /
        // DIVXU.W / RTS / BSR d:8, whose sizes have nothing to do with a modulus.
        } else {
            // Other instructions (branch, jump, system) are typically word-sized
            instr.size = 1;
        }
        
        return instr;
    }
    
    std::string H8S2350InstructionDecoder::getMnemonic(const H8S2350Instruction& instr)
    {
        std::string mnemonic;
        
        switch (instr.opcode) {
            // Move instructions
            case H8S2350Opcode::MOV_B: mnemonic = "MOV.B"; break;
            case H8S2350Opcode::MOV_W: mnemonic = "MOV.W"; break;
            case H8S2350Opcode::MOV_L: mnemonic = "MOV.L"; break;
            
            // Arithmetic instructions
            // BUG38: ADD_B/ADD_W/ADD_L/SUB_B (0x03-0x06) retired - that range is the
            // CCR group (LDC/ORC/XORC/ANDC), rendered pages 794/800/803/807.
            case 0x03: mnemonic = "LDC Rs,CCR/EXR"; break;
            case 0x04: mnemonic = "ORC #xx:8,CCR";  break;
            case 0x05: mnemonic = "XORC #xx:8,CCR"; break;
            case 0x06: mnemonic = "ANDC #xx:8,CCR"; break;
            case H8S2350Opcode::SUB_W: mnemonic = "SUB.W"; break;
            case H8S2350Opcode::SUB_L: mnemonic = "SUB.L"; break;
            case H8S2350Opcode::CMP_B: mnemonic = "CMP.B"; break;
            case H8S2350Opcode::CMP_W: mnemonic = "CMP.W"; break;
            case H8S2350Opcode::CMP_L: mnemonic = "CMP.L"; break;
            
            // Logical instructions
            case H8S2350Opcode::AND_B: mnemonic = "AND.B"; break;
            case H8S2350Opcode::AND_W: mnemonic = "AND.W"; break;
            case H8S2350Opcode::AND_L: mnemonic = "AND.L"; break;
            // BUG84: OR_B (0x0F) retired - 0x0F is DAA Rd / MOV.L ERs,ERd.
            case H8S2350Opcode::OR_W: mnemonic = "OR.W"; break;
            case H8S2350Opcode::OR_L: mnemonic = "OR.L"; break;
            case H8S2350Opcode::XOR_B: mnemonic = "XOR.B"; break;
            case H8S2350Opcode::XOR_W: mnemonic = "XOR.W"; break;
            // BUG51: XOR_L/NOT_B/NOT_W retired - 0x14/0x15/0x16 are OR.B/XOR.B/AND.B Rs,Rd.
            case 0x14: mnemonic = "OR.B Rs,Rd"; break;
            case 0x15: mnemonic = "XOR.B Rs,Rd"; break;
            case 0x16: mnemonic = "AND.B Rs,Rd"; break;
            case H8S2350Opcode::NOT_L: mnemonic = "NOT"; break;   // 0x17, all three sizes
            
            // Shift instructions (legacy mnemonic mapping) - disabled when SSoT active
            #if !H8S_DISABLE_LEGACY_SHIFT_TABLE
            case H8S2350Opcode::SHAL_B: mnemonic = "SHAL.B"; break;
            case H8S2350Opcode::SHAL_W: mnemonic = "SHAL.W"; break;
            case H8S2350Opcode::SHAL_L: mnemonic = "SHAL.L"; break;
            case H8S2350Opcode::SHAR_B: mnemonic = "SHAR.B"; break;
            case H8S2350Opcode::SHAR_W: mnemonic = "SHAR.W"; break;
            case H8S2350Opcode::SHAR_L: mnemonic = "SHAR.L"; break;
            case H8S2350Opcode::SHLL_B: mnemonic = "SHLL.B"; break;
            case H8S2350Opcode::SHLL_W: mnemonic = "SHLL.W"; break;
            case H8S2350Opcode::SHLL_L: mnemonic = "SHLL.L"; break;
            case H8S2350Opcode::SHLR_B: mnemonic = "SHLR.B"; break;
            case H8S2350Opcode::SHLR_W: mnemonic = "SHLR.W"; break;
            case H8S2350Opcode::SHLR_L: mnemonic = "SHLR.L"; break;
            #endif
            
            // Rotate instructions
            case H8S2350Opcode::ROTL_B: mnemonic = "ROTL.B"; break;
            case H8S2350Opcode::ROTL_W: mnemonic = "ROTL.W"; break;
            case H8S2350Opcode::ROTL_L: mnemonic = "ROTL.L"; break;
            case H8S2350Opcode::ROTR_B: mnemonic = "ROTR.B"; break;
            case H8S2350Opcode::ROTR_W: mnemonic = "ROTR.W"; break;
            case H8S2350Opcode::ROTR_L: mnemonic = "ROTR.L"; break;
            
            // Branch instructions
            case H8S2350Opcode::BRA: mnemonic = "BRA"; break;
            case H8S2350Opcode::BRN: mnemonic = "BRN"; break;
            case H8S2350Opcode::BHI: mnemonic = "BHI"; break;
            case H8S2350Opcode::BLS: mnemonic = "BLS"; break;
            case H8S2350Opcode::BCC: mnemonic = "BCC"; break;
            case H8S2350Opcode::BCS: mnemonic = "BCS"; break;
            case H8S2350Opcode::BNE: mnemonic = "BNE"; break;
            case H8S2350Opcode::BEQ: mnemonic = "BEQ"; break;
            case H8S2350Opcode::BVC: mnemonic = "BVC"; break;
            case H8S2350Opcode::BVS: mnemonic = "BVS"; break;
            case H8S2350Opcode::BPL: mnemonic = "BPL"; break;
            case H8S2350Opcode::BMI: mnemonic = "BMI"; break;
            case H8S2350Opcode::BGE: mnemonic = "BGE"; break;
            case H8S2350Opcode::BLT: mnemonic = "BLT"; break;
            case H8S2350Opcode::BGT: mnemonic = "BGT"; break;
            case H8S2350Opcode::BLE: mnemonic = "BLE"; break;
            
            // Jump and subroutine instructions
            case H8S2350Opcode::JMP: mnemonic = "JMP"; break;
            case H8S2350Opcode::JSR: mnemonic = "JSR"; break;
            case H8S2350Opcode::RTS: mnemonic = "RTS"; break;
            case H8S2350Opcode::RTE: mnemonic = "RTE"; break;
            
            // Stack instructions
            case H8S2350Opcode::PUSH_B: mnemonic = "PUSH.B"; break;
            case H8S2350Opcode::PUSH_W: mnemonic = "PUSH.W"; break;
            case H8S2350Opcode::PUSH_L: mnemonic = "PUSH.L"; break;
            case H8S2350Opcode::POP_B: mnemonic = "POP.B"; break;
            case H8S2350Opcode::POP_W: mnemonic = "POP.W"; break;
            case H8S2350Opcode::POP_L: mnemonic = "POP.L"; break;
            
            // Increment/Decrement
            case H8S2350Opcode::INC_B: mnemonic = "INC.B"; break;
            case H8S2350Opcode::INC_W: mnemonic = "INC.W"; break;
            case H8S2350Opcode::INC_L: mnemonic = "INC.L"; break;
            case H8S2350Opcode::DEC_B: mnemonic = "DEC.B"; break;
            case H8S2350Opcode::DEC_W: mnemonic = "DEC.W"; break;
            case H8S2350Opcode::DEC_L: mnemonic = "DEC.L"; break;
            
            // BUG81: the unsigned multiply/divide block, RENDERED pages 799 and 802.
            // These four used to read CLR.B / CLR.W / CLR.L and (via 0x53) nothing.
            case 0x50: mnemonic = "MULXU.B Rs,Rd";  break;
            case 0x51: mnemonic = "DIVXU.B Rs,Rd";  break;
            case 0x52: mnemonic = "MULXU.W Rs,ERd"; break;
            case 0x53: mnemonic = "DIVXU.W Rs,ERd"; break;
            // BUG62: the TST_* constants are retired - see the dispatch comment.
            case 0x55: mnemonic = "BSR d:8"; break;
            
            // System instructions
            case H8S2350Opcode::NOP: mnemonic = "NOP"; break;
            case H8S2350Opcode::SLEEP: mnemonic = "SLEEP"; break;
            // case H8S2350Opcode::TRAPA: mnemonic = "TRAPA"; break;  // Temporarily removed to fix build
            
            default: mnemonic = "UNKNOWN"; break;
        }
        
        return mnemonic;
    }
    
    uint8_t H8S2350InstructionDecoder::getInstructionSize(const H8S2350Instruction& instr)
    {
        // Basic instruction size is 2 bytes (16-bit)
        uint8_t size = 2;
        
        // Add extra bytes for immediate values, displacements, etc.
        if (instr.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            switch (instr.size) {
                case 0: size += 1; break; // Byte immediate
                case 1: size += 2; break; // Word immediate
                case 2: size += 4; break; // Long immediate
            }
        } else if (instr.addressing_mode == H8S2350AddressingMode::ABSOLUTE_ADDRESS) {
            switch (instr.size) {
                case 0: size += 1; break; // 8-bit address
                case 1: size += 2; break; // 16-bit address
                case 2: size += 4; break; // 24-bit address
            }
        } else if (instr.addressing_mode == H8S2350AddressingMode::REGISTER_INDIRECT_DISP) {
            size += 2; // 16-bit displacement
        } else if (instr.addressing_mode == H8S2350AddressingMode::REGISTER_INDIRECT_INDEX) {
            size += 1; // 8-bit displacement
        } else if (instr.addressing_mode == H8S2350AddressingMode::PC_RELATIVE) {
            size += 1; // 8-bit displacement
        } else if (instr.addressing_mode == H8S2350AddressingMode::PC_RELATIVE_INDEX) {
            size += 1; // 8-bit displacement
        }
        
        return size;
    }
    
    bool H8S2350InstructionDecoder::isValidInstruction(uint16_t instruction)
    {
        uint8_t opcode = extractOpcode(instruction);
        
        // Check if opcode is in valid range
        return opcode <= H8S2350Opcode::STRC;
    }
    
    uint8_t H8S2350InstructionDecoder::extractOpcode(uint16_t instruction)
    {
        return (instruction >> 8) & 0xFF;
    }
    
    uint8_t H8S2350InstructionDecoder::extractAddressingMode(uint16_t instruction)
    {
        return (instruction >> 4) & 0x0F;
    }
    
    uint8_t H8S2350InstructionDecoder::extractSourceOperand(uint16_t instruction)
    {
        return (instruction >> 4) & 0x0F;
    }
    
    uint8_t H8S2350InstructionDecoder::extractDestinationOperand(uint16_t instruction)
    {
        return instruction & 0x0F;
    }
    
    // ==== Helper Functions for Instruction Execution ====
    
    // Get register value based on size
    uint32_t getRegisterValue(const H8S2350Registers& regs, uint8_t reg_num, uint8_t size)
    {
        uint8_t idx = reg_num & 0x07;
        // BYTE REGISTER FIELD, Renesas H8S/2350 HM Rev 3.00, Appendix A.2 legend,
        // RENDERED page 771: 0000-0111 = R0H..R7H, 1000-1111 = R0L..R7L.
        // So bit 3 SET means the LOW half. This was inverted here - `(reg_num & 0x08)`
        // selected rh - while other handlers in this same file used `(rnibble < 8)`,
        // the correct sense, citing the manual. The tree contradicted itself.
        // Measured 2026-09-13: `73 59` = BTST #5,R1L read R1H (0xF7) instead of R1L
        // (0x00), so bit 5 always read as set and the firmware's DMA-complete poll at
        // 0x010C58 span forever. (The 16-bit row on the same page - 1000-1111 = E0..E7 -
        // IS what the size==1 branch below does, so only the byte case was wrong.)
        if (size == 0) { // Byte
            return (reg_num & 0x08) ? regs.rl[idx] : regs.rh[idx];
        } else if (size == 1) { // Word
            return (reg_num & 0x08) ? regs.e[idx] : regs.r[idx];
        } else if (size == 2) { // Long
            return regs.er[idx];
        }
        return 0;
    }
    
    // Set register value based on size
    void setRegisterValue(H8S2350Registers& regs, uint8_t reg_num, uint32_t value, uint8_t size)
    {
        uint8_t idx = reg_num & 0x07;
        // Same legend as getRegisterValue(): bit 3 SET = the LOW half (rendered p.771).
        if (size == 0) { // Byte
            if (reg_num & 0x08) {
                regs.rl[idx] = value & 0xFF;   // 1000-1111 = R0L..R7L
            } else {
                regs.rh[idx] = value & 0xFF;   // 0000-0111 = R0H..R7H
            }
            regs.r[idx] = (regs.rh[idx] << 8) | regs.rl[idx];
            regs.er[idx] = (regs.er[idx] & 0xFFFF0000) | regs.r[idx];
        } else if (size == 1) { // Word
            if (reg_num & 0x08) {
                regs.e[idx] = value & 0xFFFF;
                regs.er[idx] = (static_cast<uint32_t>(regs.e[idx]) << 16) | (regs.er[idx] & 0x0000FFFF);
            } else {
                regs.r[idx] = value & 0xFFFF;
                regs.rl[idx] = regs.r[idx] & 0xFF;
                regs.rh[idx] = (regs.r[idx] >> 8) & 0xFF;
                regs.er[idx] = (regs.er[idx] & 0xFFFF0000) | regs.r[idx];
            }
        } else if (size == 2) { // Long
            regs.er[idx] = value;
            regs.e[idx] = (regs.er[idx] >> 16) & 0xFFFF;
            regs.r[idx] = regs.er[idx] & 0xFFFF;
            regs.rl[idx] = regs.r[idx] & 0xFF;
            regs.rh[idx] = (regs.r[idx] >> 8) & 0xFF;
        }
        if (idx == 7) {
            regs.sp = regs.er[7] & 0x00FFFFFF;
        }
    }
    
    // ===================================================================
    // BUG81, 2026-09-17 - THE MULTIPLY/DIVIDE FAMILY, ONE BODY, OFF THE PAGE.
    //
    // Operation, RENDERED PDF pages 775 (printed "739", MULXU/MULXS) and 776
    // (printed "740", DIVXU/DIVXS):
    //
    //   MULXU.B Rs,Rd    Rd8 x Rs8   -> Rd16                    I- H- N- Z- V- C-
    //   MULXU.W Rs,ERd   Rd16 x Rs16 -> ERd32                   I- H- N- Z- V- C-
    //   MULXS.B Rs,Rd    Rd8 x Rs8   -> Rd16   (signed)         I- H- N* Z* V- C-
    //   MULXS.W Rs,ERd   Rd16 x Rs16 -> ERd32  (signed)         I- H- N* Z* V- C-
    //   DIVXU.B Rs,Rd    Rd16 / Rs8   -> Rd16  (RdH: remainder, RdL: quotient)
    //   DIVXU.W Rs,ERd   ERd32 / Rs16 -> ERd32 (Ed:  remainder, Rd:  quotient)
    //   DIVXS.B / DIVXS.W   the same shapes, signed division
    //
    // THE DIVIDE FLAGS DESCRIBE THE **DIVISOR**, NOT THE RESULT, and that is
    // not a typo - two rendered pages say it independently. Notes [6]/[7] on
    // RENDERED PDF page 792 (printed "756"):
    //     [6] Set to 1 when the divisor is negative; otherwise cleared to 0.
    //     [7] Set to 1 when the divisor is zero;     otherwise cleared to 0.
    //     [8] Set to 1 when the quotient is negative; otherwise cleared to 0.
    // and Table A.4 on RENDERED PDF page 839 (printed "803"):
    //     DIVXU   H-  N*  Z*  V-  C-    N = Sm         Z = /Sm . /Sm-1 ... /S0
    //     DIVXS   H-  N*  Z*  V-  C-    N = Sm./Dm + /Sm.Dm     (sign of quotient)
    //                                   Z = /Sm . /Sm-1 ... /S0
    // So for DIVXU, N is the DIVISOR's MSB and Z says the DIVISOR was zero.
    // MULXU writes nothing at all. H, V and C are untouched by every row here -
    // do NOT route any of this through updateFlags(), which assigns all four.
    //
    // EACH FIELD USES THE LEGEND FOR ITS OWN OPERAND SIZE (RENDERED page 807,
    // printed 771): `rs` in a .B row is an 8-BIT register (0-7 = RnH, 8-15 =
    // RnL) while the `rd` beside it names the 16-BIT destination (0-7 = Rn,
    // 8-15 = En); in a .W row `rs` is 16-bit and `0:erd` is three bits.
    //
    // STATED APPROXIMATIONS, because Appendix A does not specify either case
    // and the Programming Manual that would is not in this repo:
    //   - DIVISOR ZERO: the destination is left UNCHANGED and [7]'s Z is set.
    //     Reported once per form so it can never be silent.
    //   - QUOTIENT OVERFLOW (a 32/16 divide whose quotient exceeds 16 bits):
    //     the quotient is truncated to the destination width. Also reported.
    // If either ever fires on a live path, settle it before trusting the run.
    // ===================================================================
    static bool executeMulDivBlock(uint8_t sub,              // 0x50..0x53
                                   bool     isSigned,
                                   uint8_t  operandByte,     // rs in 7-4, rd/0:erd in 3-0
                                   H8S2350Registers& regs,
                                   H8SFlags& flags,
                                   uint32_t pc_of_instruction)
    {
        const uint8_t rsField = uint8_t((operandByte >> 4) & 0x0F);
        const uint8_t rdField = uint8_t(operandByte & 0x0F);
        const bool    isWord  = (sub & 0x02) != 0;   // 0x52/0x53 are the .W rows
        const bool    isDiv   = (sub & 0x01) != 0;   // 0x51/0x53 are the divides

        static bool s_zeroReported[4]  = { false, false, false, false };
        static bool s_ovfReported[4]   = { false, false, false, false };

        if (!isDiv) {
            // ---- MULXU / MULXS -------------------------------------------
            if (!isWord) {
                // Rd8 x Rs8 -> Rd16.  Rs is a byte register; Rd16 holds the
                // multiplicand in its low half and receives the 16-bit product.
                const uint8_t  a   = uint8_t(getRegisterValue(regs, rsField, 0));
                const uint16_t dst = uint16_t(getRegisterValue(regs, rdField, 1));
                const uint8_t  b   = uint8_t(dst & 0xFFu);
                const uint16_t res = isSigned ? uint16_t(int16_t(int8_t(a)) * int16_t(int8_t(b)))
                                              : uint16_t(uint16_t(a) * uint16_t(b));
                setRegisterValue(regs, rdField, res, 1);
                if (isSigned) { flags.negative = (res & 0x8000u) != 0; flags.zero = (res == 0); }
                // MULXU: not one condition code. H, V, C: untouched either way.
            } else {
                // Rd16 x Rs16 -> ERd32.  Rd16 is the LOW HALF of the ERd named
                // by the three-bit `0:erd`; Rs16 is a four-bit 16-bit field.
                const uint16_t a   = uint16_t(getRegisterValue(regs, rsField, 1));
                const uint8_t  erd = uint8_t(rdField & 0x07);
                const uint16_t b   = uint16_t(regs.er[erd] & 0xFFFFu);
                const uint32_t res = isSigned ? uint32_t(int32_t(int16_t(a)) * int32_t(int16_t(b)))
                                              : uint32_t(uint32_t(a) * uint32_t(b));
                setRegisterValue(regs, erd, res, 2);
                if (isSigned) { flags.negative = (res & 0x80000000u) != 0; flags.zero = (res == 0); }
            }
            return true;
        }

        // ---- DIVXU / DIVXS -----------------------------------------------
        if (!isWord) {
            // Rd16 / Rs8 -> RdH: remainder, RdL: quotient
            const uint8_t  divisor  = uint8_t(getRegisterValue(regs, rsField, 0));
            const uint16_t dividend = uint16_t(getRegisterValue(regs, rdField, 1));

            // Z describes the DIVISOR in both forms and is written whether or not
            // the division happens. N is note [6] (the divisor's sign) for DIVXU
            // and note [8] (the quotient's sign) for DIVXS - so the signed form's
            // N is written below, after there IS a quotient, and is left alone
            // when there is not. Inventing one would be a fabrication.
            if (!isSigned) flags.negative = ((divisor & 0x80u) != 0);
            flags.zero = (divisor == 0);

            if (divisor == 0) {
                if (!s_zeroReported[sub & 0x03]) {
                    s_zeroReported[sub & 0x03] = true;
                    printf("[DIVX-ZERO] %s at PC 0x%06X: divisor is ZERO. Appendix A does not "
                           "define the result; the destination is left UNCHANGED and Z is set. "
                           "This is a STATED APPROXIMATION - if the firmware depends on it, "
                           "settle it before trusting the run.\n",
                           isSigned ? "DIVXS.B" : "DIVXU.B", pc_of_instruction);
                    fflush(stdout);
                }
                return true;
            }

            uint16_t quotient, remainder;
            if (isSigned) {
                const int16_t sdividend = int16_t(dividend);
                const int8_t  sdivisor  = int8_t(divisor);
                const int16_t q = int16_t(sdividend / sdivisor);
                const int16_t r = int16_t(sdividend % sdivisor);
                quotient  = uint16_t(uint8_t(q & 0xFF));
                remainder = uint16_t(uint8_t(r & 0xFF));
                flags.negative = (q < 0);                       // note [8]: quotient negative
                if (q > 127 || q < -128) {
                    if (!s_ovfReported[sub & 0x03]) {
                        s_ovfReported[sub & 0x03] = true;
                        printf("[DIVX-OVF] DIVXS.B at PC 0x%06X: quotient %d does not fit in 8 "
                               "bits; truncated. STATED APPROXIMATION.\n", pc_of_instruction, int(q));
                        fflush(stdout);
                    }
                }
            } else {
                const uint16_t q = uint16_t(dividend / divisor);
                const uint16_t r = uint16_t(dividend % divisor);
                if (q > 0xFF && !s_ovfReported[sub & 0x03]) {
                    s_ovfReported[sub & 0x03] = true;
                    printf("[DIVX-OVF] DIVXU.B at PC 0x%06X: quotient 0x%04X does not fit in 8 "
                           "bits; truncated. STATED APPROXIMATION.\n", pc_of_instruction, unsigned(q));
                    fflush(stdout);
                }
                quotient  = uint16_t(q & 0xFFu);
                remainder = uint16_t(r & 0xFFu);
            }
            setRegisterValue(regs, rdField, uint16_t((remainder << 8) | quotient), 1);
            return true;
        }

        // ERd32 / Rs16 -> Ed: remainder, Rd: quotient
        const uint16_t divisor  = uint16_t(getRegisterValue(regs, rsField, 1));
        const uint8_t  erd      = uint8_t(rdField & 0x07);
        const uint32_t dividend = regs.er[erd];          // read BEFORE any write: `rs` may
                                                         // name the E half of this same ERd.
        if (!isSigned) flags.negative = ((divisor & 0x8000u) != 0);   // note [6]
        flags.zero = (divisor == 0);                                  // note [7], both forms

        if (divisor == 0) {
            if (!s_zeroReported[sub & 0x03]) {
                s_zeroReported[sub & 0x03] = true;
                printf("[DIVX-ZERO] %s at PC 0x%06X: divisor is ZERO. Appendix A does not define "
                       "the result; the destination is left UNCHANGED and Z is set. This is a "
                       "STATED APPROXIMATION - if the firmware depends on it, settle it before "
                       "trusting the run.\n", isSigned ? "DIVXS.W" : "DIVXU.W", pc_of_instruction);
                fflush(stdout);
            }
            return true;
        }

        uint32_t quotient, remainder;
        if (isSigned) {
            const int32_t sdividend = int32_t(dividend);
            const int16_t sdivisor  = int16_t(divisor);
            const int32_t q = sdividend / sdivisor;
            const int32_t r = sdividend % sdivisor;
            quotient  = uint32_t(uint16_t(q & 0xFFFF));
            remainder = uint32_t(uint16_t(r & 0xFFFF));
            flags.negative = (q < 0);                          // note [8]
            if ((q > 32767 || q < -32768) && !s_ovfReported[sub & 0x03]) {
                s_ovfReported[sub & 0x03] = true;
                printf("[DIVX-OVF] DIVXS.W at PC 0x%06X: quotient %d does not fit in 16 bits; "
                       "truncated. STATED APPROXIMATION.\n", pc_of_instruction, int(q));
                fflush(stdout);
            }
        } else {
            const uint32_t q = dividend / uint32_t(divisor);
            const uint32_t r = dividend % uint32_t(divisor);
            if (q > 0xFFFFu && !s_ovfReported[sub & 0x03]) {
                s_ovfReported[sub & 0x03] = true;
                printf("[DIVX-OVF] DIVXU.W at PC 0x%06X: quotient 0x%08X does not fit in 16 bits; "
                       "truncated. STATED APPROXIMATION.\n", pc_of_instruction, unsigned(q));
                fflush(stdout);
            }
            quotient  = q & 0xFFFFu;
            remainder = r & 0xFFFFu;
        }
        setRegisterValue(regs, erd, (remainder << 16) | quotient, 2);
        return true;
    }

    // Update flags based on result
    void updateFlags(H8SFlags& flags, uint32_t result, uint8_t size, bool carry = false, bool overflow = false)
    {
        uint32_t mask = (size == 0) ? 0xFF : (size == 1) ? 0xFFFF : 0xFFFFFFFF;
        result &= mask;
        
        flags.zero = (result == 0);
        uint32_t sign_bit = (mask + 1u) >> 1; // 0x80, 0x8000, 0x80000000
        flags.negative = (result & sign_bit) != 0;
        flags.carry = carry;
        flags.overflow = overflow;
    }
    
    // ==== Advanced Addressing Mode Helpers ====
    
    // Calculate effective address based on addressing mode
    uint32_t calculateEffectiveAddress(const H8S2350Instruction& instruction, 
                                      H8S2350Emulator* emulator, 
                                      bool is_source)
    {
        auto& regs = emulator->getRegisters();
        uint8_t operand = is_source ? instruction.source_operand : instruction.destination_operand;
        
        switch (instruction.addressing_mode) {
            case H8S2350AddressingMode::REGISTER_DIRECT:
                return 0; // Not used for memory access
                
            case H8S2350AddressingMode::REGISTER_INDIRECT:
                return regs.r[operand];
                
            case H8S2350AddressingMode::REGISTER_INDIRECT_POST:
                return regs.r[operand];
                
            case H8S2350AddressingMode::REGISTER_INDIRECT_PRE:
                return regs.r[operand] - getOperandSize(instruction.size);
                
            case H8S2350AddressingMode::REGISTER_INDIRECT_DISP:
                return regs.r[operand] + instruction.displacement;
                
            case H8S2350AddressingMode::REGISTER_INDIRECT_INDEX:
                return regs.r[operand] + (instruction.displacement & 0xFF) + 
                       regs.r[instruction.source_operand];
                
            case H8S2350AddressingMode::ABSOLUTE_ADDRESS:
                return instruction.immediate_value;
                
            case H8S2350AddressingMode::IMMEDIATE:
                return 0; // Not used for memory access
                
            case H8S2350AddressingMode::PC_RELATIVE:
                return regs.pc + (static_cast<int8_t>(instruction.displacement & 0xFF));
                
            case H8S2350AddressingMode::PC_RELATIVE_INDEX:
                return regs.pc + (static_cast<int8_t>(instruction.displacement & 0xFF)) + 
                       regs.r[operand];
                
            default:
                return 0;
        }
    }
    
    // Read value from effective address
    uint32_t readFromAddress(uint32_t address, uint8_t size, H8S2350Emulator* emulator)
    {
        switch (size) {
            case 0: return emulator->readMemory8(address);
            case 1: return emulator->readMemory16(address);
            case 2: return emulator->readMemory32(address);
            default: return 0;
        }
    }
    
    // Write value to effective address
    void writeToAddress(uint32_t address, uint32_t value, uint8_t size, H8S2350Emulator* emulator)
    {
        switch (size) {
            case 0: emulator->writeMemory8(address, value & 0xFF); break;
            case 1: emulator->writeMemory16(address, value & 0xFFFF); break;
            case 2: emulator->writeMemory32(address, value); break;
        }
    }
    
    // Update register after addressing mode operation
    void updateRegisterAfterAddressing(H8S2350Instruction& instruction, 
                                      H8S2350Emulator* emulator, 
                                      bool is_source)
    {
        auto& regs = emulator->getRegisters();
        uint8_t operand = is_source ? instruction.source_operand : instruction.destination_operand;
        
        switch (instruction.addressing_mode) {
            case H8S2350AddressingMode::REGISTER_INDIRECT_POST:
                regs.r[operand] += getOperandSize(instruction.size);
                emulator->syncRegAfterWordWrite(operand);
                break;
                
            case H8S2350AddressingMode::REGISTER_INDIRECT_PRE:
                regs.r[operand] -= getOperandSize(instruction.size);
                emulator->syncRegAfterWordWrite(operand);
                break;
        }
    }
    
    // ==== Instruction Executor Implementation ====
    
    // FIX27c: register-direct bit operations, Renesas HM:
    //   0x70 BSET #xx:3,Rd | 0x71 BNOT | 0x72 BCLR | 0x73 BTST  (2 bytes: 7x [0iii|rd])
    // Replaces the fictional MOVA.L executors (MOVA = H8SX-only, not in the 2350 manual).
    static bool executeRegBitOp(const H8S2350Instruction& instruction,
                                H8S2350Emulator* emulator)
    {
        auto& regs  = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        const uint8_t bit = instruction.source_operand & 0x07;
        const uint8_t rd  = instruction.destination_operand & 0x0F;
        uint8_t v = static_cast<uint8_t>(getRegisterValue(regs, rd, 0));
        switch (instruction.opcode & 0xFF) {
            case 0x70: v |=  (1u << bit); setRegisterValue(regs, rd, v, 0); break; // BSET
            case 0x71: v ^=  (1u << bit); setRegisterValue(regs, rd, v, 0); break; // BNOT
            case 0x72: v &= ~(1u << bit); setRegisterValue(regs, rd, v, 0); break; // BCLR
            case 0x73: flags.zero = ((v >> bit) & 1u) == 0; break;                 // BTST: Z = !bit

            // BLD / BILD #xx:3,Rd - HM App A.1 rendered p.761 (encoding) and p.748
            // (operation + condition codes). ONLY C is affected: I H N Z V are all
            // marked unchanged. Do not touch Z here - BTST above is the one that
            // writes Z, and conflating the two is exactly the CCR-campaign defect
            // shape that cost the Virus project a dozen rounds.
            case 0x77: {
                const bool bitset = ((v >> bit) & 1u) != 0;
                const bool invert = (instruction.source_operand & 0x08) != 0;  // BILD
                flags.carry = invert ? !bitset : bitset;
                break;
            }

            // BUG117: BOR/BIOR, BXOR/BIXOR, BAND/BIAND #xx:3,Rd. Encoding RENDERED
            // pp.794/796/797/798/799, operation p.104 of REJ09B0139: C op (bit or NOT bit)
            // -> C. ONLY C is affected; Rd is not written.
            case 0x74: case 0x75: case 0x76: {
                const bool bitset = ((v >> bit) & 1u) != 0;
                const bool invert = (instruction.source_operand & 0x08) != 0;
                const bool src    = invert ? !bitset : bitset;
                if      ((instruction.opcode & 0xFF) == 0x74) flags.carry = flags.carry || src;
                else if ((instruction.opcode & 0xFF) == 0x75) flags.carry = (flags.carry != src);
                else                                          flags.carry = flags.carry && src;
                break;
            }

            // BST / BIST #xx:3,Rd - the reverse of BLD: the carry goes INTO the bit.
            // Encoding rendered p.762, operation and flags rendered p.748:
            // NO condition code is affected - not even C, which is the source here.
            case 0x67: {
                const bool invert = (instruction.source_operand & 0x08) != 0;  // BIST
                const bool cval   = invert ? !flags.carry : flags.carry;
                if (cval) v |=  uint8_t(1u << bit);
                else      v &= uint8_t(~(1u << bit));
                setRegisterValue(regs, rd, v, 0);
                break;   // flags untouched, deliberately
            }
            default: return false;
        }
        return true;
    }

    bool H8S2350InstructionExecutor::execute(const H8S2350Instruction& instruction,
                                            H8S2350Emulator* emulator)
    {
        if (!emulator) {
            return false;
        }
        
        // Unbuffered debug via stderr (unbuffered by default)
        if (!emulator->isQuietBoot()) {
            if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-EXECUTE] opcode=0x%04X size=%d pc=0x%06X\n",
                   instruction.opcode, instruction.size, emulator->getProgramCounter());
        }
        
        // Debug log minden instruction vĂ©grehajtĂˇsĂˇhoz (csak dev-build)
#ifdef H8S_DEV_TRACE
        printf("[EXEC-DISP] opcode=0x%04X size=%d pc=0x%06X\n", 
               instruction.opcode, instruction.size, emulator->getProgramCounter());
#endif

        // BUG112, 2026-09-24. 0x1C / 0x1D are CMP.B Rs,Rd / CMP.W Rs,Rd - HM Rev 3.00 Appendix
        // A.2, RENDERED p.799 (printed 763): `1 C rs rd`, `1 D rs rd`. Flags, A.1 RENDERED p.776
        // (printed 740): Rd-Rs, H N Z V C all set; H = borrow from bit 3 (.B) / bit 11 (.W).
        // Register fields, RENDERED p.807: .B 0-7 = RnH, 8-15 = RnL; .W 0-7 = Rn, 8-15 = En.
        //
        // An intercept here used to run every encoding with (op2 & 0x88) == 0 - CMP.B RsH,RdH and
        // CMP.W Rs,Rd with R registers, the common forms - as SHAR.W / SHAR.L on Rd (SHAR is the
        // 0x11 group). It shifted the destination and set the wrong flags; the rest went to
        // executeCompare, which never read Rs. Measured by test_regfield_word before this fix:
        // `1D 34` turned ER4 0x12348000 into 0x048D2000.
        if (instruction.opcode == 0x1C || instruction.opcode == 0x1D) {
            auto& regs = emulator->getRegisters();
            auto& f    = emulator->getFlags();
            const uint32_t start_pc = instruction.decoded_pc;
            const uint8_t op2 = emulator->readByte(start_pc + 1);
            const uint8_t rs = (op2 >> 4) & 0x0F, rd = op2 & 0x0F;
            if (instruction.opcode == 0x1C) {
                const uint8_t a = (rd & 8) ? regs.rl[rd & 7] : regs.rh[rd & 7];   // Rd
                const uint8_t b = (rs & 8) ? regs.rl[rs & 7] : regs.rh[rs & 7];   // Rs
                const uint8_t r = uint8_t(a - b);
                f.carry      = b > a;
                f.half_carry = (b & 0x0F) > (a & 0x0F);
                f.zero       = r == 0;
                f.negative   = (r & 0x80) != 0;
                f.overflow   = (((a ^ b) & (a ^ r)) & 0x80) != 0;
            } else {
                const uint16_t a = (rd & 8) ? regs.e[rd & 7] : regs.r[rd & 7];
                const uint16_t b = (rs & 8) ? regs.e[rs & 7] : regs.r[rs & 7];
                const uint16_t r = uint16_t(a - b);
                f.carry      = b > a;
                f.half_carry = (b & 0x0FFF) > (a & 0x0FFF);
                f.zero       = r == 0;
                f.negative   = (r & 0x8000) != 0;
                f.overflow   = (((a ^ b) & (a ^ r)) & 0x8000) != 0;
            }
            emulator->setProgramCounter(pcMask24(start_pc + instruction.size));
            return true;
        }

        // BUG113, 2026-09-24 - THE WHOLE SHIFT/ROTATE SPACE, ONE BODY, OFF THE PAGE.
        //
        // HM Rev 3.00 Appendix A.2, RENDERED p.804 (ROTR, ROTXL, ROTXR, SHAL) and p.805 (SHAR,
        // SHLL, SHLR), ROTL on the page before: first byte 10 = SHLL/SHAL, 11 = SHLR/SHAR,
        // 12 = ROTXL/ROTL, 13 = ROTXR/ROTR. Second byte high nibble: bit 3 picks the second
        // mnemonic (SHAL/SHAR/ROTL/ROTR), bit 2 = "#2" (two bit positions), bits 1-0 = size
        // (0 = .B, 1 = .W, 3 = .L). Low nibble = rd (.B RnH/RnL, .W Rn/En - legend p.807) or
        // 0:erd (.L). Flags (A.1): N Z from the result, C = the last bit out, V = 0 except SHAL,
        // where V = 1 if the sign changed on the way (an arithmetic overflow); H unchanged.
        //
        // The old intercept here was 330 hand-written lines with a case per mnemonic, and it
        // had NO case for SHAR at all (every .B/.W/.L, 1 or 2 bits), none for SHAL.L, ROTXL.L
        // or ROTXR.L: those advanced the PC and left the register alone. Measured by
        // test_shift_all (7,680 cases): 1,464 failures before this body. The firmware's
        // pitch-to-frequency routine at 0x01340E (`SHAR.L #2,ER0` twice) therefore never
        // scaled its sum, indexed its table at 0x0242F0 four times too far, and C5 sent the
        // DSP a zero increment.
        if (instruction.opcode >= 0x10 && instruction.opcode <= 0x13) {
            auto& regs = emulator->getRegisters();
            auto& f    = emulator->getFlags();
            const uint32_t start_pc = instruction.decoded_pc;
            const uint32_t next_pc  = pcMask24(start_pc + instruction.size);
            const uint8_t  b1   = emulator->readByte(start_pc + 1);   // decoded_pc is set by decode() (line ~103)
            const uint8_t  hi   = (b1 >> 4) & 0x0F, lo = b1 & 0x0F;
            const bool     alt  = (hi & 0x8) != 0;       // SHAL / SHAR / ROTL / ROTR
            const int      cnt  = (hi & 0x4) ? 2 : 1;
            const int      szc  = hi & 0x3;              // 0 .B, 1 .W, 3 .L, 2 = no such row
            if (szc == 2 || (szc == 3 && (lo & 0x8))) {
                printf("[SHIFT-UNMATCHED] PC=0x%06X %02X %02X - not a row of Table A.2\n",
                       start_pc, instruction.opcode & 0xFF, b1);
                emulator->handleIllegalInstruction();
                return true;
            }
            const int      bits = szc == 0 ? 8 : szc == 1 ? 16 : 32;
            const uint32_t mask = bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
            const uint32_t msb  = 1u << (bits - 1);

            uint32_t v;
            if (szc == 0)      v = (lo & 8) ? regs.rl[lo & 7] : regs.rh[lo & 7];
            else if (szc == 1) v = (lo & 8) ? regs.e[lo & 7]  : regs.r[lo & 7];
            else               v = regs.er[lo & 7];

            bool c = f.carry, ovf = false;
            for (int i = 0; i < cnt; ++i) {
                const uint32_t before = v;
                switch (instruction.opcode) {
                case 0x10: c = (v & msb) != 0; v = (v << 1) & mask;                            // SHLL / SHAL
                           if (alt && ((before ^ v) & msb)) ovf = true; break;
                case 0x11: c = (v & 1) != 0; v = alt ? ((v >> 1) | (v & msb)) : (v >> 1); break; // SHAR / SHLR
                case 0x12: { const bool o = (v & msb) != 0;                                     // ROTL / ROTXL
                             v = ((v << 1) | ((alt ? o : c) ? 1u : 0u)) & mask; c = o; break; }
                default:   { const bool o = (v & 1) != 0;                                       // ROTR / ROTXR
                             v = (v >> 1) | ((alt ? o : c) ? msb : 0u); c = o; break; }
                }
            }
            f.carry    = c;
            f.negative = (v & msb) != 0;
            f.zero     = v == 0;
            f.overflow = (instruction.opcode == 0x10 && alt) ? ovf : false;

            const uint8_t idx = lo & 7;
            if (szc == 0) {
                if (lo & 8) regs.rl[idx] = uint8_t(v); else regs.rh[idx] = uint8_t(v);
                emulator->syncRegAfterByteWrite(idx, (lo & 8) == 0);
            } else if (szc == 1) {
                if (lo & 8) { regs.e[idx] = uint16_t(v); emulator->syncRegAfterUpperWordWrite(idx); }
                else        { regs.r[idx] = uint16_t(v); emulator->syncRegAfterWordWrite(idx); }
            } else {
                regs.er[idx] = v; emulator->syncRegAfterLongWrite(idx);
            }
            emulator->setProgramCounter(next_pc);
            emulator->addCycles(instruction.baseCycles);
            return true;
        }

        // Early intercept: DAA/DAS (P1.11 BCD adjust) and MULXU/S.
        //
        // BUG53, 2026-09-13: EXTU.W/.L, EXTS.W/.L and NEG.B/.W/.L were ALSO handled here,
        // which made this a SECOND, parallel owner of primary 0x17 - the "find every path
        // before adding one" trap that BUG48 cost a round to. Their bodies are deleted and
        // the single encoding-driven `case 0x17` below owns the group. Two of the deleted
        // bodies were wrong anyway: the register field was masked to THREE bits (it is
        // four - legend, rendered page 771), and NEG's half-carry was `a & 0x0F` for ALL
        // THREE sizes, where a word borrows out of bit 11 and a longword out of bit 27.
        // Dispatching on a MNEMONIC STRING is also how a decoder and an executor come to
        // disagree in silence (BUG5, BUG40) - the executor now reads the encoding.
        // BUG81, 2026-09-17: the MULXU.B / MULXS.B / MULXU.W / MULXS.W bodies that stood
        // here are DELETED. They were reached by MNEMONIC STRING - the dispatch shape this
        // very comment warns about two paragraphs up - and all four were wrong on the
        // flags: RENDERED page 775 (printed "739") gives MULXU as `I- H- N- Z- V- C-`,
        // NOT ONE condition code, and MULXS as `N* Z*` with H, V and C untouched. These
        // bodies wrote N and Z for MULXU and forced H=V=C=0 for both - a correct machine
        // fed a wrong CCR by us, the campaign shape this thread has paid for since the
        // Virus. The whole 0x50-0x53 block is now one encoding-driven case below, and the
        // signed forms live with the 0x01 prefix that actually encodes them.
        if (instruction.mnemonic.size() == 5 && (instruction.mnemonic == "DAA.B" || instruction.mnemonic == "DAS.B")) {   // PERF-MCU-1: length first
            auto& regs = emulator->getRegisters();
            auto& f = emulator->getFlags();

            auto set_nz8 = [&](uint8_t v){ f.negative = (v & 0x80u)!=0; f.zero = (v==0); };

            if (instruction.mnemonic == "DAA.B") {
                const uint8_t rd = instruction.destination_operand & 0x07;
                // Preserve incoming H/V and C unless decimal correction generates a carry
                const bool C_old = f.carry, H_old = f.half_carry, V_old = f.overflow;
                uint8_t a = static_cast<uint8_t>(regs.r[rd] & 0xFFu);
                // Digit-wise DAA per amended table: correct ones, propagate into tens, then tens correction
                uint8_t ones = (uint8_t)(a & 0x0Fu);
                uint8_t tens = (uint8_t)((a >> 4) & 0x0Fu);
                if ((ones >= 9u) || H_old) {
                    ones = (uint8_t)(ones + 6u);
                    if (ones >= 10u) { ones = (uint8_t)(ones - 10u); tens = (uint8_t)(tens + 1u); }
                }
                if ((tens >= 9u) || C_old) {
                    tens = (uint8_t)(tens + 6u);
                }
                const uint16_t composed = (uint16_t)(((tens & 0x0Fu) << 4) | (ones & 0x0Fu));
                const uint8_t result = (uint8_t)composed;
                regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | result;
                emulator->syncRegAfterWordWrite(rd);
                // Flags: N/Z from result; C set iff decimal correction overflowed whole byte; H/V unchanged
                set_nz8(result);
                f.carry = (tens > 0x0Fu) ? true : C_old; // if tens overflowed beyond 9+6 path
                f.half_carry = H_old;
                f.overflow = V_old;
                return true;
            }
            if (instruction.mnemonic == "DAS.B") {
                const uint8_t rd = instruction.destination_operand & 0x07;
                // Preserve incoming C/H/V as per P1.11 test contract
                const bool C_old = f.carry, H_old = f.half_carry, V_old = f.overflow;
                uint8_t a = static_cast<uint8_t>(regs.r[rd] & 0xFFu);

                // Decimal adjust for subtraction using only incoming borrows
                const bool borrow_in   = !C_old;  // C=1 â†’ no-borrow
                const bool h_borrow_in = !H_old;  // H=1 â†’ no-borrow (lower nibble)

                uint8_t corr = 0;
                if (h_borrow_in) corr |= 0x06;  // low-digit borrow
                if (borrow_in)   corr |= 0x60;  // high-digit borrow

                const uint8_t result = static_cast<uint8_t>((int16_t)a - (int16_t)corr);
                regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | result;
                emulator->syncRegAfterWordWrite(rd);

                // Flags: update N/Z only; preserve C/H/V
                set_nz8(result);
                f.carry = C_old;
                f.half_carry = H_old;
                f.overflow = V_old;
                return true;
            }
            // BUG53: the EXTU.W / EXTS.W / EXTU.L / EXTS.L / NEG.B / NEG.W / NEG.L bodies
            // that stood here are DELETED - primary 0x17 has one owner now, `case 0x17`
            // in the main dispatch, driven by the encoding rather than by a string.
        }

            // Early intercept: ADDX (P1.10)
        if ((instruction.mnemonic.size() == 7 || instruction.mnemonic.size() == 9) && (instruction.mnemonic == "ADDX_RR" || instruction.mnemonic == "ADDX_IMM8")) {   // PERF-MCU-1
            auto& regs = emulator->getRegisters();
            auto& flags = emulator->getFlags();

            // BUG109: THE REGISTER FIELD WAS INVERTED. Appendix A.2 legend, RENDERED page 807
            // (printed 771): 8-bit register field 0000-0111 = R0H-R7H, 1000-1111 = R0L-R7L. This
            // read 0-7 as RnL and 8-15 as RnH, so `98 00` = ADDX #0,R0L added the carry into R0H.
            // MEASURED at 0x011E38..0x011E4A: the firmware counts the set bits of ER2/ER3 with
            // SHLL.L / ADDX #0,R0L; the count stayed 0, DEC.B R1L made the loop at 0x011E52 run 256
            // times, and it walked past its address table sending "800001 <data as address> 00061C"
            // to the DSP - one of which (X:$31) pointed a voice at a scratch word, the DSP jumped
            // through it into P:$C00 with r4 = $B and zeroed its own command table (the $6C trap).
            auto get_byte = [&](uint8_t idx)->uint8_t{
                if ((idx & 0x08) == 0) return regs.rh[idx & 0x07];
                else return regs.rl[idx & 0x07];
            };
            auto set_byte = [&](uint8_t idx, uint8_t v){
                if ((idx & 0x08) == 0) {
                    regs.rh[idx & 0x07] = v;
                    emulator->syncRegAfterByteWrite(idx & 0x07, true);
                } else {
                    regs.rl[idx & 0x07] = v;
                    emulator->syncRegAfterByteWrite(idx & 0x07, false);
                }
            };

            uint8_t rd_idx = 0;
            uint8_t src = 0;
            const uint32_t cur_pc   = emulator->getProgramCounter();
            const uint32_t start_pc = pcMask24(cur_pc - instruction.size);

            if (instruction.mnemonic == "ADDX_RR") {
                rd_idx = instruction.destination_operand & 0x0F;
                const uint8_t rs_idx = instruction.source_operand & 0x0F;
                src = get_byte(rs_idx);
            } else { // ADDX_IMM8
                rd_idx = instruction.destination_operand & 0x0F;
                src = emulator->readByte(start_pc + 1);
            }

            const uint8_t dst = get_byte(rd_idx);
            const uint8_t cin = flags.carry ? 1u : 0u;
            const uint8_t res = static_cast<uint8_t>(dst + src + cin);

            set_byte(rd_idx, res);

            auto f = H8S::ADDX::flags_after(dst, src, cin, res);
            flags.half_carry = f.H;
            flags.negative   = f.N;
            // Z retention (AND): Z_new = Z_old && (res==0)
            flags.zero       = (res == 0) ? flags.zero : false;
            flags.overflow   = f.V;
            flags.carry      = f.C;
            return true;
        }

        // P1 PRODUCTION: Extended Branch Helper Functions
        auto be_disp16 = [&](uint32_t start_pc) -> int16_t {
            // 4-byte extended branch: [0x00 op_lo disp_hi disp_lo] at start_pc
            const uint8_t dhi = emulator->readByte(start_pc + 2);
            const uint8_t dlo = emulator->readByte(start_pc + 3);
            return (int16_t)((uint16_t)dhi << 8 | (uint16_t)dlo);
        };
        
        auto exec_ext_branch = [&](const char* name, bool taken) -> bool {
            const uint32_t cur_pc   = emulator->getProgramCounter();
            const uint32_t start_pc = pcMask24(cur_pc - instruction.size);
            const uint32_t next_pc  = cur_pc;
            
            // Use local utility for displacement reading
            const int16_t disp16 = be_disp16(start_pc);
            
#ifdef H8S_DEV_TRACE
            printf("[%s-TRACE] PC=0x%06X disp=%d taken=%s target=0x%06X\n",
                   name, start_pc, (int)disp16, taken ? "YES" : "NO",
                   taken ? ext_branch_target(next_pc, disp16) : next_pc);
#endif
            
            if (taken) {
                const uint32_t target = ext_branch_target(next_pc, disp16);
                emulator->setProgramCounter(target);
                emulator->addCycles(H8S_CYC_BRANCH_TAKEN);
            } else {
                emulator->setProgramCounter(next_pc);
                // No extra cycles for not-taken branch - base cycles already counted
            }
            
            return true;
        };

        // P1 PRODUCTION: Extended Branch Early Intercepts
        // All extended branches use same timing: 2 base + (taken ? 1 : 0)
        
        // P1 & P1.5 PRODUCTION: SSoT-based Extended Branch Execution (H8S/2600)
        
        // === 16-bit opcode early intercept for 0x00xx extended branches ===
        const uint32_t cur_pc = emulator->getProgramCounter();
        const uint32_t start_pc = pcMask24(cur_pc - instruction.size);
        const uint16_t opcode16 = (uint16_t(emulator->readByte(start_pc)) << 8) | emulator->readByte(start_pc + 1);
        const uint8_t opcode = opcode16 >> 8;  // High byte = actual opcode
        // === FIX27b: All ExtBr EARLY INTERCEPT REMOVED - fictional 0x00xx extended branches ===
        // H8S ISA only has real Bcc d:16 = 58 [cc]0 disp16 (handled in executeBcc16)
        // ================================================
        // === FIX13: Bit-Manipulation Groups (0x7C/7D/7E/7F) ===
        
        // Legacy P1 fallback paths removed - all extended branches now use 16-bit opcode early intercept

        // === FIX13: Bit-Manipulation Groups (0x7C/7D/7E/7F) ===
        // Handle primary opcodes 0x7C-0x7F directly from raw bytes (bypass decoder issues)
        // BUG127 (2026-09-26): 0x7C ONLY. executeBitManipGroup's 0x7D body tests `opByte & 0xC0`
        // against a register-form encoding that does not exist (7D is `0:erd 0` + an operation
        // byte: BCLR Rn,@ERd = 7D | 0:erd 0 | 6 2 | rn 0, RENDERED p.796, printed 760) and its 0x7E/0x7F bodies are `return true; // TODO` - so every
        // BSET/BCLR/BNOT Rn,@ERd and every @aa:8 bit op was a silent NOP. MEASURED: the DSP
        // parameter-slot allocator at 0x11960 (`bclr r0l,@er4` on the free-slot bitmap 0x40189A)
        // never marked a slot used, so every voice got slot 0 (X:$536/$538/$53A) - A08 sounded on
        // every 4th note, A09's OSC1 went silent after a wave change. 0x7D/0x7E/0x7F now reach the
        // 0x7C-0x7F case of the dispatch below, written from the same rendered page.
        if (opcode == 0x7C) {
            uint8_t dummy_size = instruction.size;
            if (!emulator->executeBitManipGroup(opcode, dummy_size)) {
                return false;  // executeBitManipGroup already logs error and halts
            }
            // PC advanced by executeBitManipGroup; we must adjust the instruction size for PC advance
            // Note: The PC was already advanced by instruction.size in the default path
            // Since we return true here, the caller won't advance PC again
            return true;
        }

        // Legacy P1 fallback paths removed - all extended branches now use 16-bit opcode early intercept

        // === FIX15: CMP.B (0x1C) / CMP.W (0x1D) Register-Register Compare ===
        // 0x1C = CMP.B Rs, Rd  (8-bit compare, sets N,Z,V,C from Rd - Rs)
        // 0x1D = CMP.W Rs, Rd  (16-bit compare, sets N,Z,V,C from Rd - Rs)
        // Both use executeCompare which reads instruction.size (0=byte, 1=word)
        if (instruction.opcode == 0x1C || instruction.opcode == 0x1D) {
            return executeCompare(instruction, emulator);
        }

        // === Bcc d:8 EARLY INTERCEPT (0x40-0x4F) ===
        // Renesas p.279/280 (cross-checked with kn100/cybemu disassembler): 0x40-0x4F are the
        // 8-bit conditional branches (BRA/BRN/BHI/BLS/BCC/BCS/BNE/BEQ/BVC/BVS/BPL/BMI/BGE/BLT/BGT/BLE).
        // The legacy enum table wrongly maps these values to JMP/JSR/PUSH/POP/INC/DEC, so we must
        // intercept the raw primary opcode BEFORE the enum switch below. (decoder sets source_operand=cc)
        if (instruction.opcode >= 0x40 && instruction.opcode <= 0x4F) {
            return executeBcc8(instruction, emulator);
        }

        // === Bcc d:16 EARLY INTERCEPT (0x58) ===
        // Renesas p.279/280: 0x58 is the 16-bit conditional branch group. The legacy enum
        // table wrongly mapped 0x58 to POP_RN, so intercept the raw opcode before the switch.
        if (instruction.opcode == 0x58) {
            return executeBcc16(instruction, emulator);
        }

        // === BCLR #xx:3,Rd EARLY INTERCEPT (0x72) ===
        // Renesas p.79/280: 0x72 is register-direct bit-clear. The legacy decoder mis-mapped it to
        // MOVA_L and it then fell into the 0x6F-0x7F branch fallback (mis-executed as BLE16).
        if (instruction.opcode == 0x72) {
            return executeBCLR_IMM3_REG(instruction, emulator);
        }

        // === JMP @aa:24 EARLY INTERCEPT (0x5A) ===
        // The legacy JMP enum was wrongly 0x40 (that's BRA d:8); the real 0x5A JMP @aa:24 had no
        // decoder, so the reset-thread's main-loop entry "JMP @0x002000" silently fell through.
        if (instruction.opcode == 0x5A) {
            return executeJMP_AA24(instruction, emulator);
        }

        // === MOV.B #xx:8,Rd EARLY INTERCEPT (0xF0-0xFF) ===
        // Renesas h8s2600 p.154/284: the entire 0xF0-0xFF range is MOV.B immediate-to-register.
        // The legacy switch mapped 0xF0/F4/F6/F7/F8/FC/FE to ROTR/BCLR/BAND/BOR/BXOR/BTST, which
        // mis-decoded firmware bytes like "F8 1B" (MOV.B #0x1B,R0L) as BXOR. execute0xF9Instruction
        // already handles the whole range generically, so route all of 0xF0-0xFF to it here.
        if (instruction.opcode >= 0xF0 && instruction.opcode <= 0xFF) {
            return execute0xF9Instruction(instruction, emulator);
        }

        try {
            switch (instruction.opcode) {
                // Move instructions
                // BUG56: MOV_B = 0x7F is RETIRED - it was on the AUDIT-QUEUE and this is
                // the fifth time a fictional enum constant has defended itself with a
                // C2196 duplicate-case error. 0x7F is the @aa:8 memory bit-manipulation
                // group (RENDERED page 799); MOV.B has no 0x7F row anywhere in Appendix
                // A.1. MOV_W = 0x01 (always a PREFIX) and MOV_L = 0x02 (STC CCR,Rd) are
                // still on that queue and are left only because nothing has forced them
                // into the open yet.
                // BUG112: MOV_L (0x02) removed - 0x02 is STC.B CCR/EXR,Rd, executed by `case 0x02`
                // below off RENDERED p.805.
                case H8S2350Opcode::MOV_W:
                    return executeMove(instruction, emulator);
                
                // Arithmetic instructions.
                // BUG38: ADD_B(0x03)/ADD_W(0x04)/ADD_L(0x05)/SUB_B(0x06) enum values are
                //   WRONG - 0x03-0x07 is the CCR group (LDC/ORC/XORC/ANDC), rendered
                //   pages 794/800/803/807, dispatched above.
                // BUG93: SUB_L (= 0x08) WAS THE LAST SURVIVOR OF THAT FICTIONAL FAMILY,
                //   and it was wrong in exactly the same way. RENDERED page 774 (printed
                //   738), Appendix A (2): primary 0x08 is ADD.B Rs,Rd - "Rd8 + Rs8 ->
                //   Rd8", two bytes, H/N/Z/V/C all changed. It is not a subtract of any
                //   size. Sending it here to executeSubtract() is what left R0L at 0x00
                //   when the firmware ran ADD.B R3H,R0L at 0x00472E. Now dispatched by
                //   `case 0x08` below. The SUB_L constant itself stays defined only
                //   because a cycle-count heuristic still reads its numeric value.
                // BUG105: CMP_B (= 0x09) WAS THE SAME FICTIONAL FAMILY AGAIN. Table A.2 Instruction
                //   Codes, RENDERED page 794 (printed 758): "ADD.W Rs,Rd  W  0 9 rs rd". Table A.1
                //   (2), RENDERED page 774 (printed 738): Rd16+Rs16 -> Rd16, H[3] N Z V C, 1 state.
                //   It was executed as a COMPARE, so Rd never changed. MEASURED on the DSP boot
                //   path at 0x01089A/0x01089C, `09 80` twice = ADD.W E0,R0 twice: the stage-2
                //   byte count 3*N+6 came out as N+6 (ETCR0B = 0x1406 instead of 0x3C06), and the
                //   DMAC sent a third of the DSP's program.
                case 0x09:
                    return executeADD_W_REG_REG(instruction, emulator);
                // NOTE: CMP_W(0x0A) and CMP_L(0x0B) enum values are WRONG - 0x0A is INC.B/ADD.L
                //   (handled by case 0x0A) and 0x0B is ADDS/INC.W/INC.L (handled by case 0x0B),
                //   per Renesas p.67/132/133/134/283.
                // NOTE: AND_B(0x0C)/AND_W(0x0D) enum values are WRONG - those are MOV.B/MOV.W
                //   Rs,Rd (Renesas p.284/285), handled by case 0x0C/0x0D. Only AND_L kept here.
                case H8S2350Opcode::AND_L:
                    return executeAnd(instruction, emulator);
                // BUG84: OR_B (0x0F) retired - that primary is DAA / MOV.L ERs,ERd.
                case H8S2350Opcode::OR_W:
                case H8S2350Opcode::OR_L:
                    return executeOr(instruction, emulator);
                case H8S2350Opcode::XOR_B:      // 0x12 - NOT read off a page either; AUDIT-QUEUE
                case H8S2350Opcode::XOR_W:      // 0x13 - same
                    return executeXor(instruction, emulator);
                // BUG51: XOR_L=0x14, NOT_B=0x15 and NOT_W=0x16 are retired - those three
                // primaries are OR.B / XOR.B / AND.B Rs,Rd (rendered pages 803, 806, 794),
                // dispatched below. NOT.B/NOT.W/NOT.L are all 0x17 (rendered page 803).
                // BUG53: the whole 0x17 unary group - NOT / NEG / EXTU / EXTS, all three
                // sizes, all two bytes. Encodings RENDERED pages 803 and 800; flags
                // RENDERED pages 778 (NOT, "(3) Logical Instructions"), 776 (NEG, EXTU)
                // and 777 (EXTS):
                //
                //   NOT.B/W/L    ~Rd -> Rd            I- H- N* Z* V=0 C-
                //   EXTU.W/L     zero the high half   I- H- N=0 Z* V=0 C-
                //   EXTS.W/L     sign-extend          I- H- N* Z* V=0 C-
                //   NEG.B/W/L    0 - Rd -> Rd         I- H* N* Z* V*  C*   <- the ONLY
                //                                                            one that
                //                                                            touches H and C
                case H8S2350Opcode::NOT_L: {   // == 0x17
                    auto& r = emulator->getRegisters();
                    auto& f = emulator->getFlags();
                    const uint32_t at  = emulator->getProgramCounter() - instruction.size;
                    const uint8_t  sub = (uint8_t)instruction.source_operand & 0x0F;
                    const uint8_t  fld = (uint8_t)instruction.destination_operand & 0x0F;
                    const uint8_t  erd = fld & 0x07;

                    switch (sub) {
                        case 0x0: {   // NOT.B Rd
                            const uint8_t res = uint8_t(~(uint8_t)getRegisterValue(r, fld, 0));
                            setRegisterValue(r, fld, res, 0);
                            f.negative = (res & 0x80) != 0; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        case 0x1: {   // NOT.W Rd
                            const uint16_t res = uint16_t(~(uint16_t)getRegisterValue(r, fld, 1));
                            setRegisterValue(r, fld, res, 1);
                            if (erd == 7) emulator->syncRegAfterLongWrite(7);
                            f.negative = (res & 0x8000) != 0; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        case 0x3: {   // NOT.L ERd
                            const uint32_t res = ~r.er[erd];
                            r.er[erd] = res; emulator->syncRegAfterLongWrite(erd);
                            f.negative = (res & 0x80000000u) != 0; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        case 0x5: {   // EXTU.W Rd - zero bits 15..8 of Rd16
                            const uint16_t res = uint16_t(getRegisterValue(r, fld, 1) & 0x00FFu);
                            setRegisterValue(r, fld, res, 1);
                            if (erd == 7) emulator->syncRegAfterLongWrite(7);
                            f.negative = false; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        case 0x7: {   // EXTU.L ERd - zero bits 31..16 of ERd32
                            const uint32_t res = r.er[erd] & 0x0000FFFFu;
                            r.er[erd] = res; emulator->syncRegAfterLongWrite(erd);
                            f.negative = false; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        case 0xD: {   // EXTS.W Rd - bit 7 into bits 15..8
                            const uint16_t a = (uint16_t)getRegisterValue(r, fld, 1);
                            const uint16_t res = uint16_t(int16_t(int8_t(a & 0xFF)));
                            setRegisterValue(r, fld, res, 1);
                            if (erd == 7) emulator->syncRegAfterLongWrite(7);
                            f.negative = (res & 0x8000) != 0; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        case 0xF: {   // EXTS.L ERd - bit 15 into bits 31..16
                            const uint32_t res = uint32_t(int32_t(int16_t(r.er[erd] & 0xFFFF)));
                            r.er[erd] = res; emulator->syncRegAfterLongWrite(erd);
                            f.negative = (res & 0x80000000u) != 0; f.zero = (res == 0); f.overflow = false;
                            break;
                        }
                        // NEG is `0 - operand`, so its flags are a SUBTRACTION's, taken
                        // from the same definitions the rest of the tree uses: C is the
                        // borrow out of the MSB, H the borrow out of bit 3 / 11 / 27,
                        // and V is set only for the one value that cannot be negated.
                        case 0x8: {   // NEG.B Rd
                            const uint8_t a = (uint8_t)getRegisterValue(r, fld, 0);
                            const uint8_t res = uint8_t(0u - a);
                            setRegisterValue(r, fld, res, 0);
                            f.carry = (a != 0); f.half_carry = ((a & 0x0Fu) != 0);
                            f.overflow = (a == 0x80u);
                            f.negative = (res & 0x80) != 0; f.zero = (res == 0);
                            break;
                        }
                        case 0x9: {   // NEG.W Rd
                            const uint16_t a = (uint16_t)getRegisterValue(r, fld, 1);
                            const uint16_t res = uint16_t(0u - a);
                            setRegisterValue(r, fld, res, 1);
                            if (erd == 7) emulator->syncRegAfterLongWrite(7);
                            f.carry = (a != 0); f.half_carry = ((a & 0x0FFFu) != 0);
                            f.overflow = (a == 0x8000u);
                            f.negative = (res & 0x8000) != 0; f.zero = (res == 0);
                            break;
                        }
                        case 0xB: {   // NEG.L ERd
                            const uint32_t a = r.er[erd];
                            const uint32_t res = 0u - a;
                            r.er[erd] = res; emulator->syncRegAfterLongWrite(erd);
                            f.carry = (a != 0); f.half_carry = ((a & 0x0FFFFFFFu) != 0);
                            f.overflow = (a == 0x80000000u);
                            f.negative = (res & 0x80000000u) != 0; f.zero = (res == 0);
                            break;
                        }
                        default:
                            printf("[0x17-UNKNOWN] 0x%06X: bytes 17 %02X - sub-nibble %X is not "
                                   "in the ten rows of RENDERED pages 803/800\n",
                                   at, unsigned((sub << 4) | fld), sub);
                            emulator->halt();
                            return false;
                    }
                    if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s field=%X -> ER%d=0x%08X\n",
                           at, instruction.mnemonic.c_str(), fld, erd, r.er[erd]);
                    emulator->addCycles(2);
                    return true;
                }

                // BUG51, 2026-09-13 - the BYTE counterparts of BUG37's word group.
                // Renesas HM Rev 3.00, Appendix A.1:
                //     OR.B  Rs,Rd   1 4 | rs rd    2 bytes   RENDERED page 803
                //     XOR.B Rs,Rd   1 5 | rs rd    2 bytes   RENDERED page 806
                //     AND.B Rs,Rd   1 6 | rs rd    2 bytes   RENDERED page 794
                // `rs`/`rd` are the EIGHT-bit register field, four bits each (legend,
                // rendered page 771). Flags, rendered page 742: N and Z change, V is
                // CLEARED, C and H are UNCHANGED.
                //
                // MEASURED: the firmware's SCI1 bring-up opens with `15 88` at 0x0023C2 =
                // XOR.B R0L,R0L - the idiom for "clear R0L" - and then writes that zero
                // into SSR1, SCR1 and SMR1 with three `6A A8` stores. Decoded as the
                // fictional "NOT.B" it left R0L = 0x5B, so all three registers got 0x5B:
                // SMR1 became 7-bit, 2 stop bits, CKS=3 instead of the 8N1 CKS=0 that
                // BRR=9 needs for 31250 baud. The MIDI port was never configured, so
                // SCI1 transmitted NOTHING and LOOP-0x2F60 could never drain.
                case 0x14: case 0x15: case 0x16: {
                    auto& r = emulator->getRegisters();
                    auto& f = emulator->getFlags();
                    const uint32_t at = emulator->getProgramCounter() - instruction.size;
                    const uint8_t  rs = (uint8_t)instruction.source_operand & 0x0F;
                    const uint8_t  rd = (uint8_t)instruction.destination_operand & 0x0F;
                    const uint8_t  a  = (uint8_t)getRegisterValue(r, rd, 0);
                    const uint8_t  b  = (uint8_t)getRegisterValue(r, rs, 0);
                    const uint8_t  res = (instruction.opcode == 0x14) ? uint8_t(a | b)
                                       : (instruction.opcode == 0x15) ? uint8_t(a ^ b)
                                                                      : uint8_t(a & b);
                    setRegisterValue(r, rd, res, 0);
                    f.zero = (res == 0);
                    f.negative = (res & 0x80) != 0;
                    f.overflow = false;
                    if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s R%d%c,R%d%c = 0x%02X\n",
                           at, (instruction.opcode == 0x14) ? "OR.B" :
                               (instruction.opcode == 0x15) ? "XOR.B" : "AND.B",
                           rs & 7, (rs & 8) ? 'L' : 'H', rd & 7, (rd & 8) ? 'L' : 'H', res);
                    emulator->addCycles(2);
                    return true;
                }

                // Shift and rotate instructions (legacy executor) - disabled when SSoT active
                #if !H8S_DISABLE_LEGACY_SHIFT_TABLE
                case H8S2350Opcode::SHAL_B:
                case H8S2350Opcode::SHAL_W:
                case H8S2350Opcode::SHAL_L:
                case H8S2350Opcode::SHLL_B:
                case H8S2350Opcode::SHLL_W:
                case H8S2350Opcode::SHLL_L:
                    return executeShiftLeft(instruction, emulator);
                // NOTE: H8S2350Opcode::SHAR_B = 0x1B is WRONG - the real 0x1B is DEC.W/DEC.L/SUBS
                //   (Renesas p.115/253/283). The SHAR family is at 0x10-0x13. Handled below by case 0x1B.
                // SHAR_W/SHAR_L kept here for now (TODO verify their enum values too).
                case H8S2350Opcode::SHAR_W:
                case H8S2350Opcode::SHAR_L:
                case H8S2350Opcode::SHLR_B:
                case H8S2350Opcode::SHLR_W:
                case H8S2350Opcode::SHLR_L:
                    return executeShiftRight(instruction, emulator);

                case H8S2350Opcode::ROTL_B:
                case H8S2350Opcode::ROTL_W:
                case H8S2350Opcode::ROTL_L:
                case H8S2350Opcode::ROTXL_B:
                case H8S2350Opcode::ROTXL_W:
                case H8S2350Opcode::ROTXL_L:
                    return executeRotateLeft(instruction, emulator);
                case H8S2350Opcode::ROTR_B:
                case H8S2350Opcode::ROTR_W:
                case H8S2350Opcode::ROTR_L:
                case H8S2350Opcode::ROTXR_B:
                case H8S2350Opcode::ROTXR_W:
                case H8S2350Opcode::ROTXR_L:
                    return executeRotateRight(instruction, emulator);
                #endif

                // 0x1B = real DEC.W #1/2,Rd ; DEC.L #1/2,ERd ; SUBS #1/2/4,ERd (Renesas p.283).
                //   IMPORTANT: This case MUST be outside the H8S_DISABLE_LEGACY_SHIFT_TABLE
                //   conditional block, otherwise the entire delay-loop decrement is dead code
                //   and R0 never decreases between iterations.
                case 0x1B:
                    return execute0x1BInstruction(instruction, emulator);

                // BUG47: the Bcc block that stood here was keyed on enum values
                // 0x30-0x3F. THE REAL Bcc GROUP IS 0x40-0x4F - the decoder already
                // says so in its own comment ("40=BRA 41=BRN ... 4F=BLE, Renesas
                // p.279/280") and handles it there. 0x30-0x3F is MOV.B Rs,@aa:8
                // (RENDERED page 801), which is how this firmware drives the LCD
                // on Port 2. Dispatched above; the enum names are retired in the
                // header. Three invented tables were stacked on this range:
                // Bcc at 0x30-0x3F, shift/rotate at 0x20-0x2F, and a duplicate
                // "Rm,Rn" ALU set also at 0x20-0x2E - the enum contradicted
                // ITSELF, defining 0x20 as both MOV_RM_RN and SHLL_L.

                // Jump instructions
                case H8S2350Opcode::JMP:
                    return executeJump(instruction, emulator);
                case H8S2350Opcode::JSR:
                    return executeJumpSubroutine(instruction, emulator);
                // RTS moved to raw opcode handling (case 0x54) for 2-byte format support
                // RTE removed - H8S/2350 uses different opcodes

                // Stack instructions
                case H8S2350Opcode::PUSH_B:
                case H8S2350Opcode::PUSH_W:
                case H8S2350Opcode::PUSH_L:
                    return executePush(instruction, emulator);
                case H8S2350Opcode::POP_B:
                case H8S2350Opcode::POP_W:
                case H8S2350Opcode::POP_L:
                    return executePop(instruction, emulator);

                                  // System instructions
                                  case H8S2350Opcode::NOP: // 0x00 NOP
                                      return executeNop(instruction, emulator);
                                  case H8S2350Opcode::SLEEP:
                                      return executeSleep(instruction, emulator);
                                  // case H8S2350Opcode::TRAPA removed to fix build conflicts

                                  // Handle 0xFF firmware padding (0xFFFF) as NOP
                                  case 0xFF:
                                      return executeNopFF(instruction, emulator);

                  // Increment/Decrement instructions
                  case H8S2350Opcode::INC_B:
                  case H8S2350Opcode::INC_W:
                  case H8S2350Opcode::INC_L:
                      return executeIncrement(instruction, emulator);
                  case H8S2350Opcode::DEC_B:
                  case H8S2350Opcode::DEC_W:
                  case H8S2350Opcode::DEC_L:
                      return executeDecrement(instruction, emulator);

                  // BUG84 - MOV.L ERs,ERd, RENDERED page 802. (The DAA half of this
                  // primary is decoded to the mnemonic the early intercept already
                  // owns, so only the move needs a case here.)
                  case 0x0F: {
                      if (instruction.addressing_mode != 0x0F) break;   // DAA: handled above
                      auto& r = emulator->getRegisters();
                      auto& f = emulator->getFlags();
                      const uint8_t ers = uint8_t(instruction.source_operand & 0x07);
                      const uint8_t erd = uint8_t(instruction.destination_operand & 0x07);
                      const uint32_t v = r.er[ers];
                      emulator->setERd(erd, v);
                      // The MOV.L group's flags, RENDERED page 802: N and Z from the
                      // value, V CLEARED, C and H untouched. Do not route through an
                      // adder and do not call updateFlags(), which assigns C too.
                      f.negative = (v & 0x80000000u) != 0;
                      f.zero     = (v == 0);
                      f.overflow = false;
                      if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.L ER%d,ER%d -> 0x%08X\n",
                                                    pcMask24(emulator->getProgramCounter() - instruction.size),
                                                    ers, erd, v);
                      emulator->addCycles(2);
                      return true;
                  }

                  case 0x18:
                  case 0x19:
                  case 0x1A: { // SUB.B/W/L register forms
                      auto& regs = emulator->getRegisters();
                      auto& flags = emulator->getFlags();
                      const uint32_t pc = emulator->getProgramCounter();
                      const uint8_t spec = emulator->readByte(pc - instruction.size + 1);
                      const uint8_t rs = (spec >> 4) & 0x0F;
                      const uint8_t rd = spec & 0x0F;

                      if (instruction.opcode == 0x18) {
                          const uint8_t src = (rs & 0x08) ? regs.rl[rs & 0x07] : regs.rh[rs & 0x07];   // rendered p.771
                          const uint8_t dst = (rd & 0x08) ? regs.rl[rd & 0x07] : regs.rh[rd & 0x07];   // rendered p.771
                          const uint8_t result = static_cast<uint8_t>(dst - src);
                          // rendered p.771: bit 3 SET = the LOW half
                          if (rd & 0x08) {
                              regs.rl[rd & 0x07] = result;
                              emulator->syncRegAfterByteWrite(rd & 0x07, false);
                          } else {
                              regs.rh[rd & 0x07] = result;
                              emulator->syncRegAfterByteWrite(rd & 0x07, true);
                          }
                          flags.zero = (result == 0);
                          flags.negative = (result & 0x80u) != 0;
                          flags.carry = (src > dst);
                          flags.half_carry = (src & 0x0Fu) > (dst & 0x0Fu);   // BUG121: H = borrow at bit 3 (REJ09B0139 RENDERED p.233); it was never written
                          flags.overflow = ((dst ^ src) & (dst ^ result) & 0x80u) != 0;
                      } else if (instruction.opcode == 0x19) {
                          // ==========================================================
                          // BUG83, 2026-09-17 - `SUB.W Rs,Rd` MASKED ITS REGISTER FIELD
                          // TO THREE BITS. It is FOUR: Appendix A.2's legend, RENDERED
                          // page 807 (printed "771"), 0000-0111 = R0..R7 and
                          // 1000-1111 = E0..E7. This is BUG35's defect and BUG73's, a
                          // fourth time, and this one was on the LIVE BOOT PATH.
                          //
                          // MEASURED by TOOL-PCOFFMAP's ring, one dump, no guesswork:
                          //   0x014608  19 AA   = SUB.W E2,E2
                          //             ER2 0x00034DBA -> 0x00030000     (R2 cleared)
                          //             correct:        -> 0x00004DBA     (E2 cleared)
                          // and the four instructions after it are a jump table:
                          //   0x01460A  10 72   SHLL.L #2,ER2        index x 4
                          //   0x01460C  0A B2   ADD.L ER3,ER2        + base 0x0002B2E8
                          //   0x01460E  01 00 69 23  MOV.L @ER2,ER3  load the pointer
                          //   0x014614  5D 30   JSR @ER3             call it
                          // With E2 left standing, the "index" carried 0x0003 in its top
                          // half, the table read landed at flash 0x0EB2E8 instead, ER3
                          // came back 0x60F732B0, and the JSR left the memory map -
                          // which is exactly what [PC-OFFMAP] then reported.
                          //
                          // `getRegisterValue`/`setRegisterValue` already implement the
                          // legend (they are what BUG35 routed 0x79/0x1B/0x0B through)
                          // and were simply not used here. The 0x18 branch above is
                          // already four-bit; 0x1A's field really is `0:ers 0:erd`, so
                          // three bits there is correct. The distinction belongs to the
                          // encoding, not to taste.
                          //
                          // OWED, NOT DONE THIS ROUND: the half-carry. RENDERED page 775
                          // (printed "739") gives SUB.W as `I- H[3] N* Z* V* C*`, and
                          // note [3] on RENDERED page 792 (printed "756") is "Set to 1
                          // when a carry or borrow occurs at bit 11". No branch of this
                          // handler writes H at all. Left alone deliberately so this
                          // round's measurement stays attributable to the field fix;
                          // it is a real gap and it is recorded in the open queue.
                          // ==========================================================
                          const uint16_t src = static_cast<uint16_t>(getRegisterValue(regs, rs, 1));
                          const uint16_t dst = static_cast<uint16_t>(getRegisterValue(regs, rd, 1));
                          const uint16_t result = static_cast<uint16_t>(dst - src);
                          setRegisterValue(regs, rd, result, 1);
                          flags.zero = (result == 0);
                          flags.negative = (result & 0x8000u) != 0;
                          flags.carry = (src > dst);
                          flags.half_carry = (src & 0x0FFFu) > (dst & 0x0FFFu);   // BUG121: H = borrow at bit 11 (REJ09B0139 RENDERED p.235); it was never written
                          flags.overflow = ((dst ^ src) & (dst ^ result) & 0x8000u) != 0;
                      } else if ((spec & 0x80) != 0) {
                          // SUB.L ERs,ERd - Renesas H8S/2350 HM Rev 3.00, App A.1,
                          // RENDERED page 770: "SUB.L ERs,ERd  1 A  1:ers 0:erd".
                          // BIT 7 MUST BE SET. See the DEC branch below for why.
                          const uint8_t s = rs & 0x07;
                          const uint8_t d = rd & 0x07;
                          const uint32_t src = regs.er[s];
                          const uint32_t dst = regs.er[d];
                          const uint32_t result = dst - src;
                          emulator->setERd(d, result);
                          flags.zero = (result == 0);
                          flags.negative = (result & 0x80000000u) != 0;
                          flags.carry = (src > dst);
                          flags.half_carry = (src & 0x0FFFFFFFu) > (dst & 0x0FFFFFFFu);   // BUG121: H = borrow at bit 27 (REJ09B0139 RENDERED p.236); it was never written
                          flags.overflow = ((dst ^ src) & (dst ^ result) & 0x80000000u) != 0;
                      } else if (rs == 0x00) {
                          // ================================================================
                          // DEC.B Rd - RENDERED page 763: "DEC.B Rd  1 A  0 rd".
                          // Operation and flags, RENDERED page 739:
                          //     Rd8 - 1 -> Rd8
                          //     I -  H -  N changes  Z changes  V changes  C -
                          // C AND H ARE UNTOUCHED. SUB.L updates both. Running one as the
                          // other therefore corrupts the carry as well as the result - the
                          // same CCR-campaign shape that cost the Virus project a dozen
                          // rounds.
                          //
                          // 2026-09-13: every 0x1A fell into the SUB.L branch above,
                          // regardless of bit 7. The DECODER already knew better - it sets
                          // the mnemonic "DEC.B Rd" for this exact case - but nothing ever
                          // read that mnemonic, so the disassembly and the execution
                          // disagreed. A probe must report what the code DOES; so must a
                          // mnemonic.
                          //
                          // MEASURED in LOOP-0x11CF0, `1A 02` at 0x011D2E:
                          //     expected  ER2 FFF7DEEF -> FFF7DDEF   (DEC.B on R2H)
                          //     actual    ER2 FFF7DEEF -> FFF6DEF1   (ER2 - ER0, ER0=0xFFFE)
                          // ================================================================
                          const uint8_t idx  = rd & 0x07;
                          const bool    high = (rd & 0x08) == 0;   // 0-7 = RnH, 8-15 = RnL
                          const uint8_t dst    = high ? regs.rh[idx] : regs.rl[idx];
                          const uint8_t result = static_cast<uint8_t>(dst - 1u);
                          if (high) regs.rh[idx] = result; else regs.rl[idx] = result;
                          emulator->syncRegAfterByteWrite(idx, high);

                          flags.zero     = (result == 0);
                          flags.negative = (result & 0x80u) != 0;
                          flags.overflow = (dst == 0x80u);   // only 0x80 - 1 overflows
                          // carry and half_carry: NOT TOUCHED, per the rendered page.
                      } else {
                          // Neither form. Say so instead of quietly doing arithmetic -
                          // a branch that cannot reject is not a branch.
                          printf("[0x1A-UNKNOWN] spec=0x%02X at PC 0x%06X is neither "
                                 "SUB.L ERs,ERd (bit7 set) nor DEC.B Rd (high nibble 0)\n",
                                 spec, pc - instruction.size);
                      }
                      return true;
                  }

                  // ==========================================================
                  // BUG81 - 0x50-0x53, the unsigned multiply/divide block.
                  // RENDERED pages 799 (DIVXU rows) and 802 (MULXU rows); the
                  // operation and flags are on 775/776 with notes on 792.
                  //
                  // What stood on three of these four opcodes: CLR_B = 0x50,
                  // CLR_W = 0x51, CLR_L = 0x52, dispatched to executeClear(),
                  // which ZEROES the destination register and then puts the
                  // result through updateFlags() - assigning all four of
                  // N/Z/C/V. THERE IS NO `CLR` INSTRUCTION IN THE H8S/2350:
                  // a sweep of Appendix A finds `CLRMAC` and nothing else, and
                  // CLRMAC itself is marked "Cannot be used in the H8S/2350
                  // Group". The idiom this part uses to zero a register is
                  // `SUB.B Rd,Rd` / `XOR.B Rd,Rd` - which is exactly what the
                  // firmware writes at 0x0023C2 (BUG51) and 0x002026 (BUG49).
                  // The fourth, 0x53, was not decoded at all and halted the
                  // boot at PC 0x005E78 with [OPCODE-MISSING].
                  // ==========================================================
                  case 0x50: case 0x51: case 0x52: case 0x53: {
                      auto& r = emulator->getRegisters();
                      auto& f = emulator->getFlags();
                      const uint8_t operandByte =
                          uint8_t(((instruction.source_operand & 0x0F) << 4) |
                                   (instruction.destination_operand & 0x0F));
                      const uint32_t start_pc =
                          pcMask24(emulator->getProgramCounter() - instruction.size);
                      emulator->addCycles(instruction.baseCycles);
                      return executeMulDivBlock(uint8_t(instruction.opcode & 0xFF),
                                                false, operandByte, r, f, start_pc);
                  }
                  // BUG62: TST_B = 0x53, TST_W = 0x42 and TST_L = 0x55 are RETIRED.
                  // THERE IS NO `TST` INSTRUCTION IN THE H8S/2350. A sweep of the manual
                  // finds 56 occurrences of the letters TST and EVERY ONE of them is
                  // `TSTR`, the TPU timer-start register. The three primaries are:
                  //     0x42 = BHI d:8       (the Bcc group, 4 cc | disp)
                  //     0x53 = DIVXU.W Rs,ERd
                  //     0x55 = BSR d:8       (RENDERED page 798)
                  // and `TST_W`'s own comment confessed: "moved to avoid RTS collision".
                  // The SEVENTH time a fictional enum constant has defended itself with a
                  // C2196 duplicate-case error - this time against `BSR d:8`.

                  // Extend instructions.
                  // BUG37: EXT_W/EXTU_B/EXTU_W (0x64/0x65/0x66) were fiction and are
                  // gone - those three primaries are OR.W / XOR.W / AND.W Rs,Rd,
                  // RENDERED page 806, dispatched below. EXT_B (0x63) is kept only
                  // because removing it is a separate question (0x63 is BTST Rn,Rd);
                  // it is flagged in the header and belongs to the AUDIT-QUEUE.
                  // BUG40: EXT_B (0x63) retired too - 0x63 is BTST Rn,Rd, RENDERED
                  // page 798. executeExtend now has no dispatch at all; the real
                  // EXTS/EXTU are 0x17-prefixed and are not implemented, so the
                  // firmware reaching one will be reported rather than silently
                  // sign-extended.
                  case 0x63: {
                      auto& r = emulator->getRegisters();
                      auto& f = emulator->getFlags();
                      const uint8_t rn = (uint8_t)instruction.source_operand & 0x0F;
                      const uint8_t rd = (uint8_t)instruction.destination_operand & 0x0F;
                      const uint8_t bit = (uint8_t)(getRegisterValue(r, rn, 0) & 0x07);
                      const uint8_t v   = (uint8_t)getRegisterValue(r, rd, 0);
                      // BTST writes Z ONLY: Z = NOT(the tested bit). I H N V C are all
                      // unchanged - the same rule the 0x77 BLD work established from
                      // rendered page 748, and the reason BTST must not be routed
                      // through updateFlags().
                      f.zero = ((v >> bit) & 1u) == 0;
                      if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BTST R%d%c(bit %d),R%d%c -> Z=%d\n",
                             emulator->getProgramCounter() - instruction.size,
                             rn & 7, (rn & 8) ? 'L' : 'H', bit,
                             rd & 7, (rd & 8) ? 'L' : 'H', f.zero ? 1 : 0);
                      emulator->addCycles(2);
                      return true;
                  }

                  // Exchange instructions - EXG disabled due to 0x69 conflict with MOV.B
                  // case H8S2350Opcode::EXG:  // DISABLED: 0x69 reserved for MOV.B Rs,@ERn per H8S manual
                  //     return executeExchange(instruction, emulator);
                  // NOTE: H8S2350Opcode::SWAP was wrongly defined as 0x6A. The 0x6A primary
                  //   is MOV.B absolute + bit-manip (Renesas p.154/282), handled by case 0x6A above.
                  //   Removed the duplicate SWAP dispatch to resolve C2196 (case 106 already used).
                
                   // BUG117: 0x74/0x75/0x76 = BOR/BXOR/BAND (and inverting forms) #xx:3,Rd,
                   // dispatched with the rest of the register bit family below.
                   case 0x74: case 0x75: case 0x76:
                       return executeRegBitOp(instruction, emulator);

                   // 0x79 = immediate-word ALU group (Renesas p.156/279). Intercept before Bcc fallback.
                   // BUG93: 0x08 = ADD.B Rs,Rd (RENDERED page 774, printed 738).
                   // Decoded since forever, never dispatched - a silent 2-byte NOP.
                   case 0x08:
                       return executeADD_B_REG_REG(instruction, emulator);

                   // INC.B Rd / ADD.L ERs,ERd (Renesas p.66/132/283)
                   case 0x0A:
                       return execute0x0AInstruction(instruction, emulator);

                   // 0x0B = ADDS / INC.W / INC.L (Renesas p.67/133/134/279)
                   case 0x0B:
                       return execute0x0BInstruction(instruction, emulator);

                   // MOV.B/MOV.W Rs,Rd (Renesas p.284/285)
                   case 0x0C:
                       return executeMOV_B_REG_REG(instruction, emulator);
                   case 0x0D:
                       return executeMOV_W_REG_REG(instruction, emulator);

                   // 0x78 = MOV.B/MOV.W @(d:32,ERn) <-> Rd (Renesas p.284/285), 10 bytes.
                   //   Must intercept BEFORE the 0x6F-0x7F extended-branch fallback (which wrongly
                   //   treated 0x78 as BVC16).
                   case 0x78:
                       return execute0x78Instruction(instruction, emulator);

                   case 0x79:
                       return execute0x79Instruction(instruction, emulator);

                 // 0x7A = imm32 ALU group (MOV.L/ADD.L/CMP.L/SUB.L #xx:32). FIX27c: Bcc guard deleted.
                   // BUG55: CMP.L ERs,ERd (0x1F with bit 7 set). DAS.B - the other row -
                   // keeps its existing early-intercept handler, which is reached by the
                   // "DAS.B" mnemonic the decoder now sets, so this case only ever sees
                   // the CMP row. The result is DISCARDED; only the flags survive.
                   case 0x1F: {
                       auto& r = emulator->getRegisters();
                       auto& f = emulator->getFlags();
                       const uint8_t  ers = (uint8_t)instruction.source_operand & 0x07;
                       const uint8_t  erd = (uint8_t)instruction.destination_operand & 0x07;
                       const uint32_t a   = r.er[erd];
                       const uint32_t b   = r.er[ers];
                       const uint32_t res = a - b;
                       f.carry     = (a < b);
                       f.half_carry= ((a & 0x0FFFFFFFu) < (b & 0x0FFFFFFFu));   // borrow out of bit 27
                       f.overflow  = (((a ^ b) & (a ^ res) & 0x80000000u) != 0);
                       f.negative  = (res & 0x80000000u) != 0;
                       f.zero      = (res == 0);
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: CMP.L ER%d,ER%d "
                              "(0x%08X - 0x%08X) Z=%d N=%d C=%d\n",
                              emulator->getProgramCounter() - instruction.size, ers, erd,
                              a, b, f.zero?1:0, f.negative?1:0, f.carry?1:0);
                       emulator->addCycles(2);
                       return true;
                   }

                   // BUG56: the 0x7C-0x7F memory bit-manipulation group. One owner, and
                   // the operation byte is read off the encoding rather than guessed.
                   // Flags, all already established in this file from rendered pages 748
                   // and 798: BSET/BNOT/BCLR/BST/BIST affect NO condition code at all;
                   // BTST writes Z ONLY (Z = NOT bit); BLD/BILD and BOR/BXOR/BAND write
                   // C ONLY. Nothing here goes through updateFlags().
                   case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
                       auto& r = emulator->getRegisters();
                       auto& f = emulator->getFlags();
                       const uint32_t at = emulator->getProgramCounter() - instruction.size;
                       const uint8_t  b2 = (uint8_t)instruction.source_operand;
                       const uint8_t  op = (uint8_t)instruction.destination_operand;
                       const uint8_t  b4 = emulator->readByte(at + 3);

                       // 7C/7D take @ERd from `0:erd 0`; 7E/7F take @aa:8, and @aa:8
                       // reaches the top page only - EA = H'FFFF00 + abs (BUG47).
                       const bool     viaReg = (instruction.opcode <= 0x7D);
                       const uint32_t ea = viaReg ? (r.er[(b2 >> 4) & 0x07] & 0x00FFFFFFu)
                                                  : (0x00FFFF00u | uint32_t(b2));

                       // The operand byte is `rn 0` for the register forms and
                       // `i:IMM:0` for the immediate ones, where bit 7 is the INVERTING
                       // flag that distinguishes BILD from BLD and BIST from BST.
                       const bool    invert = (b4 & 0x80) != 0;
                       const uint8_t bitNo  = (op & 0xF0) == 0x60
                                            ? uint8_t(getRegisterValue(r, (b4 >> 4) & 0x0F, 0) & 0x07)
                                            : uint8_t((b4 >> 4) & 0x07);

                       uint8_t  v   = emulator->readByte(ea);
                       const bool bit = ((v >> bitNo) & 1u) != 0;
                       const bool src = invert ? !bit : bit;
                       const char* nm = "?";
                       bool  store = false;

                       switch (op & 0x7F) {
                           case 0x60: case 0x70: v = uint8_t(v |  (1u << bitNo)); store = true; nm = "BSET"; break;
                           case 0x61: case 0x71: v = uint8_t(v ^  (1u << bitNo)); store = true; nm = "BNOT"; break;
                           case 0x62: case 0x72: v = uint8_t(v & ~(1u << bitNo)); store = true; nm = "BCLR"; break;
                           case 0x63: case 0x73: f.zero = !bit;                                 nm = "BTST"; break;
                           case 0x67:            // BST / BIST - the bit takes C, or NOT C
                               v = uint8_t((v & ~(1u << bitNo))
                                         | (uint8_t((invert ? !f.carry : f.carry) ? 1u : 0u) << bitNo));
                               store = true;     nm = invert ? "BIST" : "BST";  break;
                           case 0x74:            f.carry = f.carry || src;       nm = invert ? "BIOR"  : "BOR";  break;
                           case 0x75:            f.carry = (f.carry != src);     nm = invert ? "BIXOR" : "BXOR"; break;
                           case 0x76:            f.carry = f.carry && src;       nm = invert ? "BIAND" : "BAND"; break;
                           case 0x77:            f.carry = src;                  nm = invert ? "BILD"  : "BLD";  break;
                           default:
                               printf("[BITOP-UNKNOWN] 0x%06X: bytes %02X %02X %02X %02X - "
                                      "operation byte %02X is not in the 0x7C-0x7F group "
                                      "(RENDERED page 799)\n",
                                      at, instruction.opcode, b2, op, b4, op);
                               emulator->halt();
                               return false;
                       }
                       if (store) emulator->writeByte(ea, v);
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s #%d,@0x%06X -> 0x%02X\n",
                              at, nm, bitNo, ea, v);
                       emulator->addCycles(6);
                       return true;
                   }

                   case 0x7A:
                       return execute0x7AGroup(instruction, emulator);

                   // BUG52: 0x7B = EEPMOV.B / EEPMOV.W (RENDERED page 799). It had a
                   // decoder entry calling itself "STC VBR, ERn" at size 2 and NO
                   // dispatch at all, so it fell to [OPCODE-MISSING] and halted.
                   case 0x7B:
                       return execute0x7BInstruction(instruction, emulator);

                   // FIX27c: 0x70-0x73 = register-direct bit ops (BSET/BNOT/BCLR/BTST #imm3,Rd)
                   case 0x70:
                   case 0x71:
                   case 0x72:
                   case 0x73:
                   // 0x77 = BLD/BILD #imm3,Rd (bit -> C). Added 2026-09-13 from the
                   // rendered manual pages 761 and 748. NOTE: 0x74 BOR/BIOR, 0x75
                   // BXOR/BIXOR and 0x76 BAND/BIAND are the rest of this family and
                   // are still unimplemented - they appear in the text extract only,
                   // which is an index, not a source. Render their page before adding.
                   case 0x77:
                   // 0x67 = BST/BIST #imm3,Rd, added 2026-09-13 from rendered p.762 + p.748.
                   case 0x67:
                       return executeRegBitOp(instruction, emulator);

                   // 0x7F handling moved to unified group decoder approach

                   // ==========================================================
                   // BUG85 - SUBX, both rows, RENDERED pages 806 (encoding),
                   // 775 (operation and flags) and 842 (the flag formulas).
                   //
                   //   SUBX Rs,Rd      1 E | rs rd     Rd8 - Rs8   - C -> Rd8
                   //   SUBX #xx:8,Rd   B rd | IMM      Rd8 - #xx:8 - C -> Rd8
                   //
                   // Table A.4, RENDERED PDF page 842 (printed "806"):
                   //   H = Sm-4 . /Dm-4 + /Dm-4 . Rm-4 + Sm-4 . Rm-4
                   //   N = Rm
                   //   Z = Z' . /Rm . ...... . /R0      <- Z' IS THE PREVIOUS Z
                   //   V = /Sm . Dm . /Rm + Sm . /Dm . Rm
                   //   C = Sm . /Dm + /Dm . Rm + Sm . Rm
                   // and note [5] on RENDERED page 792 (printed "756") states the
                   // same thing in words: "Retains its previous value when the
                   // result is zero; otherwise cleared to 0." That is the
                   // multi-precision chain, and the tree already had it right -
                   // `h8s2350_contracts.h`'s "TODO: SUBX Z-chaining behavior" is
                   // STALE and is marked so there.
                   //
                   // TWO THINGS WERE WRONG HERE.
                   //
                   // (1) THE REGISTER ACCESS WAS A HEURISTIC, NOT THE LEGEND.
                   //     It read `src` and `dst` as
                   //         rlow = r[n] & 0xFF;  rll = rl[n];
                   //         value = (rll != rlow) ? rll : rlow;
                   //     with a comment admitting why: "prefer RL if it differs
                   //     from R low byte (TESTS MAY SET EITHER)". A tie-break
                   //     between two views of the same storage, written for a
                   //     unit test, standing in for `0-7 = RnH, 8-15 = RnL`.
                   //     And the write-back was `regs.rl[rd] = res` -
                   //     UNCONDITIONALLY the low half, so every `Rn H` row wrote
                   //     the wrong byte. Now both go through getRegisterValue /
                   //     setRegisterValue at size 0, which implement the legend
                   //     and keep the mirrored views consistent.
                   //
                   // (2) V USED THE WRONG `Sm`. The code formed
                   //     `subtr8 = (src + Cin) & 0xFF` and took its bit 7 as Sm.
                   //     The page's Sm is the bit 7 of the SOURCE OPERAND alone.
                   //     `((dst^src) & (dst^res)) & 0x80` is algebraically the
                   //     page's formula: it is true exactly when Dm != Sm and
                   //     Dm != Rm, which is `/Sm.Dm./Rm + Sm./Dm.Rm`. Folding
                   //     the carry in first flips Sm in two cases - src = 0x7F
                   //     with C = 1, and src = 0xFF with C = 1 - so V came out
                   //     inverted on exactly those. Narrow, real, and the kind
                   //     of thing a borrow chain walks into.
                   //
                   // H and C are left as computed: `dst < src + Cin` and
                   // `(dst & 0xF) < (src & 0xF) + Cin` ARE the borrow-out of
                   // bit 7 and bit 3, which is what the two formulas express.
                   // ==========================================================
                   case 0x1E:
                   case 0xB0: case 0xB1: case 0xB2: case 0xB3:
                   case 0xB4: case 0xB5: case 0xB6: case 0xB7:
                   case 0xB8: case 0xB9: case 0xBA: case 0xBB:
                   case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                   {
                       auto& regs = emulator->getRegisters();
                       auto& flags = emulator->getFlags();

                       // Z' - the previous Z, which this instruction chains from.
                       const bool z_before = flags.zero;

                       const uint8_t rd = uint8_t(instruction.destination_operand & 0x0F);
                       uint8_t src;
                       if (instruction.opcode == 0x1E) {
                           src = uint8_t(getRegisterValue(regs, uint8_t(instruction.source_operand & 0x0F), 0));
                       } else {
                           src = uint8_t(instruction.immediate_value & 0xFF);
                       }

                       const uint8_t dst = uint8_t(getRegisterValue(regs, rd, 0));
                       const uint8_t cin = flags.carry ? 1 : 0;

                       const uint16_t src_c   = uint16_t(uint16_t(src) + uint16_t(cin));
                       const uint8_t  res     = uint8_t((uint16_t(dst) - src_c) & 0xFFu);

                       setRegisterValue(regs, rd, res, 0);

                       flags.carry      = (uint16_t(dst) < src_c);                       // borrow out of bit 7
                       flags.half_carry = ((dst & 0x0Fu) < ((src & 0x0Fu) + cin));       // borrow out of bit 3
                       flags.negative   = (res & 0x80u) != 0;                            // N = Rm
                       flags.overflow   = (((dst ^ src) & (dst ^ res)) & 0x80u) != 0;    // V, off the page
                       flags.zero       = (res != 0) ? false : z_before;                 // Z = Z' AND (res == 0)

                       emulator->addCycles(2);
                       return true;
                   }

                   // (ADDX handled in early intercept)


                   // Handle 0xF9 instruction (MOV.B Rs, Rd)
                   case 0xF9:
                       return execute0xF9Instruction(instruction, emulator);

                   // Handle register-indirect MOV.B instructions
                   case 0x68: // MOV.B Rs, @ERn
                       return execute0x68Instruction(instruction, emulator);

                   case 0x69: // 0x69 family: MOV.W Rs,@ERd + STC.W EXR/CCR,@ERd (pattern-based decoder)
                       if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-0x69-CASE] Calling execute0x69Family\n");
                       return execute0x69Family(instruction, emulator);

                   case 0x6C: // MOV.B @ERn, Rd
                       return execute0x6CInstruction(instruction, emulator);

                   case 0x6E: // MOV.B Rs, @(disp16, ERn)
                       return execute0x6EInstruction(instruction, emulator);


                   // BUG38: the CCR immediate group. RENDERED pages 794 (ANDC),
                   // 803 (ORC), 807 (XORC), 800 (LDC). All two bytes, second byte is
                   // the WHOLE 8-bit immediate.
                   //   ORC  0x04:  CCR | #xx:8 -> CCR
                   //   XORC 0x05:  CCR ^ #xx:8 -> CCR
                   //   ANDC 0x06:  CCR & #xx:8 -> CCR
                   //   LDC  0x07:  #xx:8       -> CCR
                   // These write the condition codes wholesale; they do not COMPUTE
                   // condition codes, so nothing else here may touch flags afterwards.
                   case 0x04: case 0x05: case 0x06: case 0x07: {
                       auto& r = emulator->getRegisters();
                       const uint32_t at = emulator->getProgramCounter() - instruction.size;
                       const uint8_t imm = emulator->readByte(at + 1);
                       // BUG121 (DIFFREF): `r.ccr & 0xFF` is the SHADOW, whose NZVC/H are stale
                       // (they live in the flags struct). ANDC #0x7F used to write old flags back.
                       const uint8_t before = emulator->ccrByteLive();
                       uint8_t after = before;
                       const char* nm = "?";
                       switch (instruction.opcode) {
                           case 0x04: after = uint8_t(before | imm); nm = "ORC";  break;
                           case 0x05: after = uint8_t(before ^ imm); nm = "XORC"; break;
                           case 0x06: after = uint8_t(before & imm); nm = "ANDC"; break;
                           default:   after = imm;                  nm = "LDC";  break;
                       }
                       emulator->setCCRFromByte(after);   // keeps ccr AND the flags struct in step
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s #0x%02X,CCR (0x%02X -> 0x%02X)\n",
                              at, nm, imm, before, after);
                       emulator->addCycles(2);
                       return true;
                   }

                   // BUG112: STC.B CCR,Rd = 02 0 rd / STC.B EXR,Rd = 02 1 rd (RENDERED
                   // p.805). Byte field per the legend (p.807). No flag is changed.
                   case 0x02: {
                       auto& r = emulator->getRegisters();
                       const uint32_t at = emulator->getProgramCounter() - instruction.size;
                       const uint8_t b2 = emulator->readByte(at + 1);
                       const bool fromEXR = (b2 & 0xF0) == 0x10;
                       if ((b2 & 0xF0) > 0x10) {
                           printf("[ERROR] 0x02 with second byte 0x%02X at 0x%06X - not in Table A.2\n", b2, at);
                           emulator->handleIllegalInstruction();
                           return true;
                       }
                       const uint8_t v = fromEXR ? uint8_t(r.exr & 0xFF) : emulator->ccrByteLive();
                       setRegisterValue(r, uint8_t(b2 & 0x0F), v, 0);
                       emulator->syncRegAfterByteWrite(b2 & 0x07, (b2 & 0x08) == 0);
                       return true;
                   }

                   // BUG38: LDC Rs,CCR = 03 0 rs / LDC Rs,EXR = 03 1 rs (RENDERED
                   // page 800). The byte register field is four bits (legend, rendered
                   // page 771), so route it through getRegisterValue with size 0.
                   case 0x03: {
                       auto& r = emulator->getRegisters();
                       const uint32_t at = emulator->getProgramCounter() - instruction.size;
                       const uint8_t b2 = emulator->readByte(at + 1);
                       const bool toEXR = (b2 & 0x10) != 0;
                       const uint8_t rs = uint8_t(((b2 & 0x10) ? (b2 & 0x0F) : (b2 & 0x0F)));
                       const uint8_t v  = (uint8_t)getRegisterValue(r, rs, 0);
                       if (toEXR) r.exr = uint16_t((r.exr & 0xFF00) | v);
                       else       emulator->setCCRFromByte(v);
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: LDC R%d%c,%s = 0x%02X\n",
                              at, rs & 7, (rs & 8) ? 'L' : 'H', toEXR ? "EXR" : "CCR", v);
                       emulator->addCycles(2);
                       return true;
                   }

                   // Handle 0x08 instruction (MOV.W Rs, Rd)
                   case 0xF0:
                       return execute0x08Instruction(instruction, emulator);

                   // ===== REGISTER OPERATIONS EXECUTORS (0x20-0x7F) =====
                   // These will be implemented as separate executor methods

                   // Handle 0x5C instruction (MOV.W @ERm, Rn)
                   case 0x5B:
                       return execute0x5BInstruction(instruction, emulator);

                   case 0x5D:
                       return execute0x5DInstruction(instruction, emulator);
                   // BUG82 - JMP @ERn, RENDERED page 800: `5 9 | 0:ern 0`, 2 bytes,
                   // no condition code affected. Its sibling `JSR @ERn` sits one line
                   // above and has been right since BUG39.
                   case 0x59: {
                       auto& r = emulator->getRegisters();
                       const uint32_t pc_after = emulator->getProgramCounter();
                       const uint32_t at       = pcMask24(pc_after - instruction.size);
                       const uint8_t  ern      = uint8_t((emulator->readByte(at + 1) >> 4) & 0x07);
                       const uint32_t target   = r.er[ern] & 0x00FFFFFF;
                       r.pc = target;
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: JMP @ER%d -> 0x%06X\n",
                                                     at, ern, target);
                       emulator->addCycles(4);
                       return true;
                   }

                   // BUG62: BSR d:8 - `5 5 | disp`, RENDERED page 798. Two bytes, the
                   // displacement signed and relative to the NEXT instruction, and no
                   // condition code affected. Same body as BSR d:16, one field narrower.
                   case 0x55: {
                       auto& r = emulator->getRegisters();
                       const uint32_t start = (emulator->getProgramCounter() - instruction.size) & 0x00FFFFFFu;
                       const int8_t   disp  = (int8_t)emulator->readByte(start + 1);
                       const uint32_t ret   = (start + 2) & 0x00FFFFFFu;
                       const uint32_t target = (ret + uint32_t(int32_t(disp))) & 0x00FFFFFFu;
                       emulator->push24(ret);
                       r.pc = target;
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BSR d:8 -> 0x%06X (disp=%d, ret=0x%06X)\n",
                              start, target, int(disp), ret);
                       emulator->addCycles(4);
                       return true;
                   }

                   case 0x5C:
                       return execute0x5CInstruction(instruction, emulator);

                   // Handle 0x5E instruction (MOV.L @ERm, ERn)
                   case 0x5E:
                       return execute0x5EInstruction(instruction, emulator);

                   // Handle 0x54 instruction (RTS - Return from Subroutine)
                   case 0x54:
                       return execute0x54Instruction(instruction, emulator);

                   // Handle 0x56 instruction (RTE or PUSH Rn)
                   case 0x56:
                       return execute0x56Instruction(instruction, emulator);

                   // Handle 0x57 instruction only when decoder identified TRAPA strictly
                   case 0x57:
                       if (instruction.mnemonic == "TRAPA") {
                           return execute0x57Instruction(instruction, emulator);
                       }
                       // Otherwise this primary is used by P1.11 (e.g., MULXS.W). Do not treat as TRAPA.
                       break;

                   // Handle 0x58 instruction (POP Rn)
                   case 0x58:
                       return execute0x58Instruction(instruction, emulator);

                   // BUG82: `case 0x59 -> ROTXL` is RETIRED. 0x59 is JMP @ERn, handled
                   // above; the ROTXL body it called is a fabrication twice over - see
                   // execute0x59Instruction()'s own tombstone.

                   // BUG57: BSET / BNOT / BCLR Rn,Rd - one handler, RENDERED page 798.
                   case 0x60: case 0x61: case 0x62:
                       return execute0x60Instruction(instruction, emulator);

                   // BUG37: OR.W / XOR.W / AND.W Rs,Rd - RENDERED page 806.
                   // Flags for the logical group, rendered page 742: N and Z change,
                   // V is CLEARED, C and H are UNCHANGED. Do not route through an adder.
                   // BUG47: MOV.B @aa:8 - the effective address is H'FFFF00 + abs
                   // (RENDERED page 801). Flags: N and Z from the byte, V cleared,
                   // C and H unchanged. The register field is the EIGHT-bit one, four
                   // bits wide (legend, rendered page 771).
                   case 0x20: case 0x21: case 0x22: case 0x23:
                   case 0x24: case 0x25: case 0x26: case 0x27:
                   case 0x28: case 0x29: case 0x2A: case 0x2B:
                   case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                   case 0x30: case 0x31: case 0x32: case 0x33:
                   case 0x34: case 0x35: case 0x36: case 0x37:
                   case 0x38: case 0x39: case 0x3A: case 0x3B:
                   case 0x3C: case 0x3D: case 0x3E: case 0x3F: {
                       auto& r = emulator->getRegisters();
                       auto& f = emulator->getFlags();
                       const uint8_t  reg   = (uint8_t)instruction.source_operand & 0x0F;
                       const uint8_t  abs8  = (uint8_t)instruction.destination_operand;
                       const uint32_t addr  = 0x00FFFF00u | uint32_t(abs8);
                       const bool     store = (instruction.opcode & 0x10) != 0;
                       uint8_t v;
                       if (store) {
                           v = (uint8_t)getRegisterValue(r, reg, 0);
                           emulator->writeByte(addr, v);
                       } else {
                           v = emulator->readByte(addr);
                           setRegisterValue(r, reg, v, 0);
                       }
                       f.zero = (v == 0);
                       f.negative = (v & 0x80) != 0;
                       f.overflow = false;
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B %s R%d%c @0x%06X = 0x%02X\n",
                              emulator->getProgramCounter() - instruction.size,
                              store ? "store" : "load", reg & 7, (reg & 8) ? 'L' : 'H', addr, v);
                       emulator->addCycles(4);
                       return true;
                   }

                   case 0x64: case 0x65: case 0x66: {
                       auto& r = emulator->getRegisters();
                       auto& f = emulator->getFlags();
                       const uint8_t rs = (uint8_t)instruction.source_operand & 0x0F;
                       const uint8_t rd = (uint8_t)instruction.destination_operand & 0x0F;
                       const uint16_t a = (uint16_t)getRegisterValue(r, rd, 1);
                       const uint16_t b = (uint16_t)getRegisterValue(r, rs, 1);
                       const uint16_t res = (instruction.opcode == 0x64) ? uint16_t(a | b)
                                          : (instruction.opcode == 0x65) ? uint16_t(a ^ b)
                                                                         : uint16_t(a & b);
                       setRegisterValue(r, rd, res, 1);
                       if ((rd & 0x07) == 7) emulator->syncRegAfterLongWrite(7);
                       f.zero = (res == 0);
                       f.negative = (res & 0x8000) != 0;
                       f.overflow = false;
                       if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s %s%d,%s%d = 0x%04X\n",
                              emulator->getProgramCounter() - instruction.size,
                              instruction.mnemonic.c_str(),
                              (rs & 8) ? "E" : "R", rs & 7, (rd & 8) ? "E" : "R", rd & 7, res);
                       emulator->addCycles(2);
                       return true;
                   }

                   // Handle 0x6A instruction (MOV.B abs + bit-manip @aa)
                   case 0x6A:
                       return execute0x6AInstruction(instruction, emulator);

                   // Handle 0x6B instruction (MOV.W abs group)
                   case 0x6B:
                       return execute0x6BInstruction(instruction, emulator);

                   // Handle 0x6D instruction (MOVU.W @ERm+, Rn)
                   case 0x6D:
                       return execute0x6DInstruction(instruction, emulator);

                   // Handle 0x6F instruction (MOVU.L @ERm+, ERn)
                   case 0x6F:
                       return execute0x6FInstruction(instruction, emulator);

                   // Sprint 2: All 0x70-0x7F cases now handled by table-driven dispatcher above

                   // ===== MEMORY OPERATIONS EXECUTORS (0x80-0xFF) =====

                   // Handle 0x80-0x8F range (MOV.B #imm8, Rn) - excluding 0x56 (PUSH) and 0x57 (TRAPA)
                   case 0x80: case 0x81: case 0x82: case 0x83:
                   // BUG121 (DIFFREF): 0x86 = ADD.B #xx:8,R6H like the rest of the row (REJ09B0139
                   // RENDERED p.48, `8 rd IMM`). The "conflicts with 0x56 PUSH" note was fiction:
                   // the 0x86 byte never met this case and executed as an illegal instruction.
                   case 0x84: case 0x85: case 0x86: case 0x87:
                   case 0x88: case 0x89: case 0x8A: case 0x8B:
                   case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                       return executeMOV_B_IMM8_Rn(instruction, emulator);

                   // 0xA0-0xAF = CMP.B #xx:8, Rd (Renesas p.107/299, cybemu cross-check).
                   //   The old BRA_AT_LABEL/JSR_AT_AA mapping was WRONG (4-byte) and broke 0x445.
                   case 0xA0: case 0xA1: case 0xA2: case 0xA3:
                   case 0xA4: case 0xA5: case 0xA6: case 0xA7:
                   case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                   case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                       return executeCMP_B_IMM8(instruction, emulator);

                   // Handle 0xC0-0xCF = OR.B #xx:8, Rd  (Renesas h8s2600 p.178)
                   case 0xC0: case 0xC1: case 0xC2: case 0xC3:
                   case 0xC4: case 0xC5: case 0xC6: case 0xC7:
                   case 0xC8: case 0xC9: case 0xCA: case 0xCB:
                   case 0xCC: case 0xCD: case 0xCE: case 0xCF:
                       return executeOR_B_IMM8(instruction, emulator);

                   // Handle 0xD0-0xDF = XOR.B #xx:8, Rd  (Renesas h8s2600 logical group)
                   case 0xD0: case 0xD1: case 0xD2: case 0xD3:
                   case 0xD4: case 0xD5: case 0xD6: case 0xD7:
                   case 0xD8: case 0xD9: case 0xDA: case 0xDB:
                   case 0xDC: case 0xDD: case 0xDE: case 0xDF:
                       return executeXOR_B_IMM8(instruction, emulator);

                   // Handle 0xE0-0xEF = AND.B #xx:8, Rd  (Renesas p.279/289)
                   case 0xE0: case 0xE1: case 0xE2: case 0xE3:
                   case 0xE4: case 0xE5: case 0xE6: case 0xE7:
                   case 0xE8: case 0xE9: case 0xEA: case 0xEB:
                   case 0xEC: case 0xED: case 0xEE: case 0xEF:
                       return executeAND_B_IMM8(instruction, emulator);

                   // NOTE: 0xE9 belongs to the 0xE0-0xEF AND.B #imm8,Rd range (Renesas p.279).
                   // The old case 0xE9 -> XOR.W was a wrong guess and duplicated the range; removed.

                   // Handle 0xF0 instruction (ROTR ERn) - DUPLICATE REMOVED
                   // This case was already handled in register operations section

                   // Handle 0xF4 instruction (BCLR #imm3, @(disp, ERn))
                   case 0xF4:
                       return executeBCLR_IMM3_AT_DISP_ERN(instruction, emulator);

                   // Handle 0xF6 instruction (BAND #imm3, @(disp, ERn))
                   case 0xF6:
                       return executeBAND_IMM3_AT_DISP_ERN(instruction, emulator);

                   // Handle 0xF7 instruction (BOR #imm3, @(disp, ERn))
                   case 0xF7:
                       return executeBOR_IMM3_AT_DISP_ERN(instruction, emulator);

                   // Handle 0xF8 instruction (BXOR #imm3, @(disp, ERn))
                   case 0xF8:
                       return executeBXOR_IMM3_AT_DISP_ERN(instruction, emulator);

                   // Handle 0xFC instruction (BTST #imm3, Rn)
                   case 0xFC:
                       return executeBTST_IMM3_RN(instruction, emulator);

                   // Handle 0xFE instruction (BXOR #imm3, Rn)
                   case 0xFE:
                       return executeBXOR_IMM3_RN(instruction, emulator);

                 default:
                    // DIAGNOSTIC GUARDS & MISROUTE DETECTION
                    uint32_t pc = emulator->getProgramCounter();
                    uint16_t full_word = emulator->readWord(pc - instruction.size);
                    uint8_t primary_op = instruction.opcode & 0xFF;
                    
                    // Handle 0x00 prefix extended instructions
                    if ((instruction.opcode & 0xFF00) == 0x0000) {
                        uint8_t extended_op = instruction.opcode & 0xFF;
                        
                        // FIX27c: the 0x6F-0x7B "extended branch" routers DELETED.
                        // They were the live teleport: any opcode 0x0070-0x007B landing
                        // here (e.g. real BSET #imm3,Rd after a decode bug) was executed
                        // as a fictional 16-bit branch reading its own bytes as the
                        // displacement (the 0x44D6 -> 0xAF82 "BLE16" with disp=0x6AA8).
                        // H8S ISA truth: real Bcc d:16 = 0x58 [cc]0 disp16, nothing else.
                        
#ifdef H8S_DEV_FAILSAFE
                        // DIAGNOSTIC: Check for potential misroute of extended ALU (debug only)
                        if (extended_op >= 0x80 && extended_op <= 0x9F) {
                            printf("[DIAG-MISROUTE] Extended ALU 0x00%02X reached default case at PC 0x%06X\n",
                                   extended_op, pc - instruction.size);
                            printf("[DIAG-MISROUTE] This should have been routed to executeExtendedALU!\n");
                            return executeExtendedALU(instruction, emulator);
                        }
#endif
                        
                        // Other extended instructions (0x1C, 0x4A, 0x5A, 0x5C, 0x20, 0x00, 0x45, 0x5E, 0x0A, 0x0B, 0xF9)
                        if (extended_op == 0x1C || extended_op == 0x4A || extended_op == 0x5A || extended_op == 0x5C ||
                            extended_op == 0x20 || extended_op == 0x00 || extended_op == 0x45 || extended_op == 0x5E ||
                            extended_op == 0x0A || extended_op == 0x0B || extended_op == 0xF9) {
                            // Side-effect probe for unknown extended instructions
                            auto& regs_before = emulator->getRegisters();
                            auto& flags_before = emulator->getFlags();
                            
                            // Capture state before (for comparison)
                            uint32_t pc_before = pc;
                            uint32_t sp_before = regs_before.sp;
                            bool z_before = flags_before.zero;
                            bool n_before = flags_before.negative;
                            
                            printf("[UNKNOWN-EXT] 0x00%02X at PC=0x%06X (failsafe NOP)\n", extended_op, pc - instruction.size);
                            printf("[PROBE] Before: SP=0x%06X Z=%d N=%d R0=0x%04X ER0=0x%08X\n", 
                                   sp_before, z_before, n_before, regs_before.r[0], regs_before.er[0]);
                            
                            // Execute as NOP (no operation) - safe fallback
                            // PC will be incremented by instruction.size automatically
                            
                            printf("[PROBE] After: No state changes (NOP simulation)\n");
                            return true;
                        }
                        
                        // Unknown extended instruction
                        printf("[DIAG-ERROR] Unknown extended instruction 0x00%02X at PC 0x%06X\n",
                               extended_op, pc - instruction.size);
                    }
                    
                    // DIAGNOSTIC: Detect potential routing conflicts for standard opcodes
                    bool potential_conflict = false;
                    const char* conflict_reason = "";
                    
                    // Check for opcodes that should have been handled by specific cases
                    if (primary_op >= 0x70 && primary_op <= 0x7F) {
                        potential_conflict = true;
                        conflict_reason = "0x70-7F opcode should have been handled by specific case or global Bcc guard";
                    } else if (primary_op >= 0x80 && primary_op <= 0x8F) {
                        potential_conflict = true;
                        conflict_reason = "0x80-8F MOV.B #imm8,Rn should have been handled by range case";
                    } else if (primary_op >= 0xA0 && primary_op <= 0xA7) {
                        potential_conflict = true;
                        conflict_reason = "0xA0-A7 BRA @label should have been handled by range case";
                    }
                    
                    if (potential_conflict) {
                        printf("[DIAG-CONFLICT] Routing conflict for opcode 0x%02X at PC 0x%06X\n", 
                               primary_op, pc - instruction.size);
                        printf("[DIAG-CONFLICT] Reason: %s\n", conflict_reason);
                        
                        // Special debug for 0x7F BLE16
                        if (primary_op == 0x7F) {
                            if (!g_h8s_quiet_boot) printf("[DEBUG-ROUTING] 0x7F instruction details: opcode=0x%04X, size=%d, baseCycles=%d\n",
                                   instruction.opcode, instruction.size, instruction.baseCycles);
                        }
                    }
                    
                    // Handle illegal/unimplemented instructions with enhanced diagnostics
                    static int unknown_opcode_log_limit = 20; // Increased for more diagnostic data
                    if (unknown_opcode_log_limit > 0) {
                        printf("[OPCODE-MISSING] 0x%04X at PC 0x%06X | Primary: 0x%02X | Size: %d | SP: 0x%06X\n",
                               (int)instruction.opcode, pc - instruction.size, primary_op, 
                               instruction.size, emulator->getRegisters().sp);
                        printf("[OPCODE-MISSING] Raw bytes: [%02X %02X] | Next: [%02X %02X]\n",
                               (full_word >> 8) & 0xFF, full_word & 0xFF,
                               emulator->readByte(pc), emulator->readByte(pc + 1));
                        unknown_opcode_log_limit--;
                        
                        // Additional diagnostic info for first few unknown opcodes
                        if (unknown_opcode_log_limit > 15) {
                            auto& regs = emulator->getRegisters();
                            printf("[OPCODE-MISSING] Context: R0=0x%04X R1=0x%04X R2=0x%04X R3=0x%04X\n",
                                   regs.r[0], regs.r[1], regs.r[2], regs.r[3]);
                        }
                    }

                    // For debugging - could also trigger exception instead of halting
                    printf("[DIAG-HALT] Halting execution due to unhandled instruction\n");
                    emulator->halt();
                    return false;
            }
        } catch (const std::exception& e) {
            std::cout << "Error executing instruction: " << e.what() << std::endl;
            return false;
        }
    }
    
    // ==== Individual Instruction Implementations ====
    
    bool H8S2350InstructionExecutor::executeMove(const H8S2350Instruction& instruction,
                                                 H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // BUG81 - the 0x01-prefixed SIGNED multiply/divide rows, RENDERED pages 799
        // (DIVXS) and 802 (MULXS). Same four sub-ops as the bare block, same body;
        // only the sign convention and the instruction length differ.
        if (instruction.opcode == 0x01 && instruction.addressing_mode == 0x51) {
            const uint32_t at = pcMask24(pc - instruction.size);
            emulator->addCycles(instruction.baseCycles);
            return executeMulDivBlock(uint8_t(instruction.source_operand & 0xFF),
                                      true,
                                      uint8_t(instruction.destination_operand & 0xFF),
                                      regs, flags, at);
        }

        // 0x01-prefix LDC #imm8 to EXR/CCR (01 41 07 IMM = LDC #imm8,EXR; 01 40 07 IMM = CCR)
        if (instruction.opcode == 0x01 && instruction.addressing_mode == 0x01) {
            // BUG74: the whole prefixed immediate group - ORC/XORC/ANDC/LDC on EXR.
            // Operation and FLAGS, Renesas HM Rev 3.00 Table A.1 (7) System Control
            // Instructions, RENDERED PDF page 791 (printed "755"):
            //     ANDC #xx:8,CCR   CCR & #xx:8 -> CCR    I* H* N* Z* V* C*   len 2
            //     ANDC #xx:8,EXR   EXR & #xx:8 -> EXR    -  -  -  -  -  -    len 4
            //     ORC / XORC identical in shape;  LDC #xx:8,EXR  -> EXR, no CC, len 4
            // So the EXR forms affect NOT ONE condition code - EXR is not the CCR, and
            // nothing here may touch the flags struct. The CCR forms replace all six,
            // which is what setCCRFromByte() is for.
            const uint32_t at = pc - instruction.size;
            const uint8_t sub  = emulator->readByte(at + 2);   // 04 ORC / 05 XORC / 06 ANDC / 07 LDC
            const uint8_t imm8 = (uint8_t)instruction.source_operand;

            if (instruction.destination_operand != 1) {
                // `01 40 0x` - NOT A ROW IN APPENDIX A.2. The CCR forms are the UNPREFIXED
                // two-byte ones. Report with the bytes instead of inventing an instruction;
                // that habit is what produced MOVA.L, MOVU.L, STC VBR and the LDC on 0x68.
                static uint32_t seen40 = 0;
                if (seen40 < 8) {
                    ++seen40;
                    printf("[LDC-EXT-NO-SUCH-ROW] 0x%06X: bytes 01 40 %02X %02X - Appendix A.2 has no "
                           "`01 40 0x` form (the CCR forms are the 2-byte unprefixed ones). NOT executed.\n",
                           at, sub, imm8);
                }
                emulator->addCycles(2);
                return true;
            }

            const uint8_t before = uint8_t(regs.exr & 0xFF);
            uint8_t after = before;
            const char* nm = "?";
            switch (sub) {
                case 0x04: after = uint8_t(before | imm8); nm = "ORC";  break;
                case 0x05: after = uint8_t(before ^ imm8); nm = "XORC"; break;
                case 0x06: after = uint8_t(before & imm8); nm = "ANDC"; break;
                default:   after = imm8;                   nm = "LDC";  break;
            }
            regs.exr = uint16_t((regs.exr & 0xFF00) | after);
            if (!g_h8s_quiet_boot)
                printf("[EXECUTE] 0x%06X: %s #0x%02X,EXR (0x%02X -> 0x%02X, mask %u -> %u)\n",
                       at, nm, imm8, before, after,
                       unsigned(before & 0x07), unsigned(after & 0x07));
            emulator->addCycles(2);
            return true;
        }
        // 0x01-prefix STM.L/LDM.L (register block save/restore) - Renesas h8s2600 p.284/288.
        if (instruction.opcode == 0x01 && (instruction.addressing_mode == 0x10 || instruction.addressing_mode == 0x11)) {
            int count = (int)instruction.source_operand + 1;   // selector 1/2/3 -> 2/3/4 registers
            uint8_t baseN = (uint8_t)instruction.destination_operand;
            bool isStore = (instruction.addressing_mode == 0x10);
            uint32_t base = pc - instruction.size;

            // 2026-09-13: the register list must lie wholly inside ER0..ER6. ER7 IS THE STACK
            // POINTER and can never be a member - loading it from the stack is what derailed
            // the boot for months. The old code wrapped the index with &7 and did it silently.
            // A guard that cannot reject is not a guard: report and refuse.
            if (baseN > 6 || baseN + count - 1 > 6) {
                printf("[STM/LDM-BAD] 0x%06X: %s list ER%d..ER%d is INVALID (ER7 is SP). "
                       "Refusing to execute - decode is wrong, not the firmware.\n",
                       base, isStore ? "STM" : "LDM", baseN, baseN + count - 1);
                return true;
            }
            if (isStore) {
                // STM.L (ERn..ERn+count-1), @-SP : push in increasing-register order, each pre-decrement 4.
                for (int k = 0; k < count; k++) {
                    uint8_t reg = uint8_t(baseN + k);   // guarded above; no silent wrap
                    uint32_t sp = (emulator->getRegisters().sp - 4) & 0x00FFFFFF;
                    emulator->getRegisters().sp = sp;
                    emulator->setSP24(sp);
                    uint32_t val = emulator->getRegisters().er[reg];
                    emulator->writeLong(sp, val);
                }
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: STM.L (ER%d..ER%d),@-SP  SP=0x%06X\n",
                       base, baseN, (baseN + count - 1) & 7, emulator->getRegisters().sp);
            } else {
                // LDM.L @SP+, (ERn..ERn+count-1) : pop in DECREASING-register order, each post-increment 4.
                for (int k = count - 1; k >= 0; k--) {
                    uint8_t reg = uint8_t(baseN + k);   // guarded above; no silent wrap
                    uint32_t sp = emulator->getRegisters().sp & 0x00FFFFFF;
                    uint32_t val = emulator->readLong(sp);
                    emulator->setERd(reg, val);
                    sp = (sp + 4) & 0x00FFFFFF;
                    emulator->getRegisters().sp = sp;
                    emulator->setSP24(sp);
                }
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: LDM.L @SP+,(ER%d..ER%d)  SP=0x%06X\n",
                       base, baseN, (baseN + count - 1) & 7, emulator->getRegisters().sp);
            }
            return true;
        }
        // =====================================================================
        // BUG34, 2026-09-13 - THE ENTIRE `01 00` MOV.L GROUP WAS A SILENT NO-OP.
        //
        // What stood here was "decode-only for now (size is correct so PC
        // advances)". The size WAS correct, so nothing ever looked wrong: every
        // longword load and store the firmware makes through a prefixed MOV.L
        // simply did not happen, and the machine walked on. That is the shape
        // this project keeps paying for - the silent SHLL fall-through (BUG28)
        // and the `>= 8` register guards were the same thing. A default that
        // keeps the machine moving is a fabrication with better manners.
        //
        // Found from STACK-0x1CE6E: `01 00 6D F0` at 0x01CE04 is PUSH.L ER0 and
        // is answered by `01 00 6D 70` (POP.L ER0) at 0x01CE3C. Measured with
        // MS2K_SPTRACE: SP was 0xFFFBF8 before AND after the push. The routine's
        // own RTS then popped from a stack two bytes out of place - the whole
        // "one word high" mystery.
        //
        // Encodings, Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED PDF
        // page 802 (printed "page 766 of 988"), read off the image:
        //
        //   MOV.L @ERs,ERd          0 1 0 0 | 6 9 | 0:ers 0:erd              4
        //   MOV.L ERs,@ERd          0 1 0 0 | 6 9 | 1:erd 0:ers              4
        //   MOV.L @ERs+,ERd         0 1 0 0 | 6 D | 0:ers 0:erd              4
        //   MOV.L ERs,@-ERd         0 1 0 0 | 6 D | 1:erd 0:ers              4
        //   MOV.L @(d:16,ERs),ERd   0 1 0 0 | 6 F | 0:ers 0:erd | disp16     6
        //   MOV.L ERs,@(d:16,ERd)   0 1 0 0 | 6 F | 1:erd 0:ers | disp16     6
        //   MOV.L @aa:16,ERd        0 1 0 0 | 6 B | 0     0:erd | abs16      6
        //   MOV.L @aa:32,ERd        0 1 0 0 | 6 B | 2     0:erd | abs32      8
        //   MOV.L ERs,@aa:16        0 1 0 0 | 6 B | 8     0:ers | abs16      6
        //   MOV.L ERs,@aa:32        0 1 0 0 | 6 B | A     0:ers | abs32      8
        //   MOV.L @(d:32,ERs),ERd   0 1 0 0 | 7 8 | 0:ers 0 | 6 B 2 0:erd | disp32   10
        //   MOV.L ERs,@(d:32,ERd)   0 1 0 0 | 7 8 | 0:erd 0 | 6 B A 0:ers | disp32   10
        //
        // POP.L ERn / PUSH.L ERn are the ERs=ER7 / ERd=ER7 cases of the 6D rows
        // (RENDERED page 803: `01 00 6D 7 0:ern` and `01 00 6D F 0:ern`), so they
        // need no separate branch - which is the point of modelling the encoding
        // rather than the two mnemonics the firmware happens to use.
        //
        // Flags for every row (MOV): N and Z from the transferred value, V = 0,
        // C and H UNCHANGED. Do not route these through an adder.
        // =====================================================================
        if (instruction.opcode == 0x01 && instruction.addressing_mode == 0x03) {
            auto& regs  = emulator->getRegisters();
            auto& flags = emulator->getFlags();
            const uint32_t base = pc - instruction.size;
            const uint8_t  b3   = emulator->readByte(base + 2);
            const uint8_t  b4   = emulator->readByte(base + 3);

            auto rd32 = [&](uint32_t a) -> uint32_t {
                return (uint32_t(emulator->readByte(a + 0)) << 24)
                     | (uint32_t(emulator->readByte(a + 1)) << 16)
                     | (uint32_t(emulator->readByte(a + 2)) <<  8)
                     |  uint32_t(emulator->readByte(a + 3));
            };
            auto wr32 = [&](uint32_t a, uint32_t v) {
                emulator->storeLong(a, v);   // BUG126: two x16 bus cycles into CS0
            };

            bool     store   = false;
            bool     handled = true;
            uint8_t  erData  = 0;
            uint32_t address = 0;
            int      preDec  = 0;   // 6D store: decrement the address register first
            int      postInc = 0;   // 6D load:  increment it after

            switch (b3) {
                case 0x69: {                                  // register indirect
                    store  = (b4 & 0x80) != 0;
                    erData =  b4 & 0x07;
                    address = regs.er[(b4 >> 4) & 0x07] & 0x00FFFFFFu;
                    break;
                }
                case 0x6D: {                                  // post-increment / pre-decrement
                    store  = (b4 & 0x80) != 0;
                    erData =  b4 & 0x07;
                    const uint8_t erAddr = (b4 >> 4) & 0x07;
                    if (store) { preDec = erAddr + 1; }        // +1 so 0 can mean "none"
                    else       { postInc = erAddr + 1; }
                    break;
                }
                case 0x6F: {                                  // 16-bit displacement
                    store  = (b4 & 0x80) != 0;
                    erData =  b4 & 0x07;
                    const int16_t disp = int16_t((uint16_t(emulator->readByte(base + 4)) << 8)
                                               |  uint16_t(emulator->readByte(base + 5)));
                    address = (regs.er[(b4 >> 4) & 0x07] + uint32_t(int32_t(disp))) & 0x00FFFFFFu;
                    break;
                }
                case 0x6B: {                                  // absolute
                    const uint8_t nib = (b4 >> 4) & 0x0F;      // 0/2 load, 8/A store; 2/A = 32-bit
                    store  = (nib & 0x08) != 0;
                    erData =  b4 & 0x07;
                    if (nib & 0x02) {
                        address = ((uint32_t(emulator->readByte(base + 4)) << 24)
                                 | (uint32_t(emulator->readByte(base + 5)) << 16)
                                 | (uint32_t(emulator->readByte(base + 6)) <<  8)
                                 |  uint32_t(emulator->readByte(base + 7))) & 0x00FFFFFFu;
                    } else {
                        // @aa:16 is SIGN-extended to the 24-bit address, so 0xFFxx
                        // reaches the on-chip I/O page rather than 0x00FFxx.
                        const int16_t a16 = int16_t((uint16_t(emulator->readByte(base + 4)) << 8)
                                                  |  uint16_t(emulator->readByte(base + 5)));
                        address = uint32_t(int32_t(a16)) & 0x00FFFFFFu;
                    }
                    break;
                }
                case 0x78: {                                  // 32-bit displacement
                    const uint8_t erAddr = (b4 >> 4) & 0x07;
                    const uint8_t b6     = emulator->readByte(base + 5);   // 2 = load, A = store
                    store  = ((b6 >> 4) & 0x0F) == 0x0A;
                    erData =  b6 & 0x07;
                    const uint32_t disp = (uint32_t(emulator->readByte(base + 6)) << 24)
                                        | (uint32_t(emulator->readByte(base + 7)) << 16)
                                        | (uint32_t(emulator->readByte(base + 8)) <<  8)
                                        |  uint32_t(emulator->readByte(base + 9));
                    address = (regs.er[erAddr] + disp) & 0x00FFFFFFu;
                    break;
                }
                default:
                    handled = false;
                    break;
            }

            if (!handled) {
                // Do NOT advance quietly past a form we cannot decode. An unknown
                // member of an otherwise implemented group is exactly what hid for
                // months here.
                printf("[MOVL-EXT-UNKNOWN] 01 00 %02X %02X at PC 0x%06X (size=%d) - not in the "
                       "Appendix A.1 MOV.L group as read from RENDERED page 802. Halting rather "
                       "than inventing an instruction.\n", b3, b4, base, instruction.size);
                emulator->halt();
                return false;
            }

            uint32_t value;
            if (store) {
                value = regs.er[erData] & 0xFFFFFFFFu;
                if (preDec) {
                    const uint8_t erAddr = uint8_t(preDec - 1);
                    regs.er[erAddr] = (regs.er[erAddr] - 4) & 0xFFFFFFFFu;
                    emulator->syncRegAfterLongWrite(erAddr);
                    address = regs.er[erAddr] & 0x00FFFFFFu;
                }
                wr32(address, value);
            } else {
                if (postInc) {
                    const uint8_t erAddr = uint8_t(postInc - 1);
                    address = regs.er[erAddr] & 0x00FFFFFFu;
                    value = rd32(address);
                    regs.er[erAddr] = (regs.er[erAddr] + 4) & 0xFFFFFFFFu;
                    emulator->syncRegAfterLongWrite(erAddr);
                } else {
                    value = rd32(address);
                }
                regs.er[erData] = value;
                emulator->syncRegAfterLongWrite(erData);
            }

            flags.zero     = (value == 0);
            flags.negative = (value & 0x80000000u) != 0;
            flags.overflow = false;

            if (!g_h8s_quiet_boot) {
                printf("[EXECUTE] 0x%06X: MOV.L %s ER%d @0x%06X = 0x%08X (SP=0x%06X)\n",
                       base, store ? "store" : "load", erData, address, value,
                       regs.er[7] & 0x00FFFFFFu);
            }
            emulator->addCycles(H8S_CYC_BASE_MEM);
            return true;
        }

        // BUG74 - the 0x01-prefix LDC/STC MEMORY group, now read off the rendered pages.
        //
        // Encodings, Renesas HM Rev 3.00 Table A.2, RENDERED PDF page 800 (LDC, printed
        // "764"), page 801 (LDC @aa:32) and page 805 (STC, printed "769"):
        //
        //   01 40 = CCR is the operand        01 41 = EXR is the operand
        //   3rd byte selects the addressing mode; BIT 7 OF THE 4TH BYTE selects the
        //   direction (0 = LDC, load into the control register; 1 = STC, store it out) -
        //   the same rule as 0x69/0x6C/0x68 everywhere else in this ISA:
        //
        //     69 | 0:ers 0            LDC @ERs            4     69 | 1:erd 0   STC @ERd
        //     6D | 0:ers 0            LDC @ERs+           4     6D | 1:erd 0   STC @-ERd
        //     6F | 0:ers 0 | disp16   LDC @(d:16,ERs)     6     6F | 1:erd 0   STC
        //     6B | 0 0     | abs16    LDC @aa:16          6     6B | 8 0       STC
        //     6B | 2 0     | abs32    LDC @aa:32          8     6B | A 0       STC
        //     78 | 0:ers 0 | 6B 2 0 | disp32   LDC @(d:32,ERs)  10
        //     78 | 0:erd 0 | 6B A 0 | disp32   STC              10
        //
        //   THE 0x78 FORM IS THE EXCEPTION: bit 7 of the 4th byte is 0 for BOTH directions
        //   and the SIXTH byte (`2 0` load / `A 0` store) decides. Read it off the page;
        //   applying the bit-7 rule there would have been a silent wrong direction.
        //
        // Operation and FLAGS, Table A.1 (7), RENDERED page 790 (LDC) and 791 (STC):
        //     LDC ... ,CCR    -> CCR      I* H* N* Z* V* C*    (all six replaced)
        //     LDC ... ,EXR    -> EXR      -  -  -  -  -  -
        //     STC CCR/EXR,... -> memory   -  -  -  -  -  -     (every STC row)
        //   and the pointer forms move the register by TWO, not four:
        //     LDC @ERs+   @ERs -> CCR, ERs32+2 -> ERs32
        //     STC @-ERd   ERd32-2 -> ERd32, CCR -> @ERd
        //
        // WHAT THE FIRMWARE ACTUALLY USES, measured in flash.bin below 0x40000 at even
        // addresses (an H8S instruction cannot start on an odd one):
        //     0x010BE8   01 40 6D F0   STC.W CCR,@-ER7   followed by 01 00 6D F0 PUSH.L ER0
        //     0x010C40   01 40 6D 70   LDC.W @ER7+,CCR   followed by 56 70       RTE
        //   - one save/restore pair, in the same module as 10712-BWAIT. Nothing else in
        //   the group appears as code.
        //
        // STATED APPROXIMATION, because this document does not settle it: the operand size
        // is W, the control register is 8 bits, and Appendix A does not say WHICH HALF of
        // the word carries it. The high byte is used here (big-endian, the order the
        // exception frame uses). It cannot change behaviour for the only two sites that
        // execute, because they are a round trip - store and load use the same half - and
        // nothing else reads that stack word. If a third site ever appears, settle the half
        // before trusting it.
        if (instruction.opcode == 0x01 && instruction.addressing_mode == 0x02) {
            const uint32_t at  = pc - instruction.size;
            const bool     toEXR = (emulator->readByte(at + 1) == 0x41);
            const uint8_t  b3  = emulator->readByte(at + 2);
            const uint8_t  b4  = emulator->readByte(at + 3);
            const uint8_t  ern = uint8_t((b4 >> 4) & 0x07);

            bool     store = (b4 & 0x80) != 0;
            uint32_t address = 0;
            bool     ok = true;

            switch (b3) {
                case 0x69:
                    address = regs.er[ern] & 0x00FFFFFFu;
                    break;
                case 0x6D:
                    if (store) {                       // STC @-ERd : pre-decrement by TWO
                        regs.er[ern] = (regs.er[ern] - 2) & 0xFFFFFFFFu;
                        emulator->syncRegAfterLongWrite(ern);
                        address = regs.er[ern] & 0x00FFFFFFu;
                    } else {                           // LDC @ERs+ : post-increment by TWO
                        address = regs.er[ern] & 0x00FFFFFFu;
                        regs.er[ern] = (regs.er[ern] + 2) & 0xFFFFFFFFu;
                        emulator->syncRegAfterLongWrite(ern);
                    }
                    break;
                case 0x6F: {
                    const int16_t disp = int16_t((emulator->readByte(at + 4) << 8) |
                                                  emulator->readByte(at + 5));
                    address = (regs.er[ern] + int32_t(disp)) & 0x00FFFFFFu;
                    break;
                }
                case 0x6B: {
                    // b4: 00 = LDC @aa:16, 80 = STC @aa:16, 20 = LDC @aa:32, A0 = STC @aa:32
                    if (b4 & 0x20) {                   // @aa:32
                        address = ((uint32_t)emulator->readByte(at + 4) << 24) |
                                  ((uint32_t)emulator->readByte(at + 5) << 16) |
                                  ((uint32_t)emulator->readByte(at + 6) << 8)  |
                                   (uint32_t)emulator->readByte(at + 7);
                        address &= 0x00FFFFFFu;
                    } else {                           // @aa:16, SIGN-extended (as MOV.L's row)
                        const int16_t a16 = int16_t((emulator->readByte(at + 4) << 8) |
                                                     emulator->readByte(at + 5));
                        address = uint32_t(int32_t(a16)) & 0x00FFFFFFu;
                    }
                    break;
                }
                case 0x78: {
                    // direction is in the SIXTH byte, not bit 7 of the fourth
                    const uint8_t b6 = emulator->readByte(at + 5);
                    store = (b6 & 0x80) != 0;          // 0x20 = load, 0xA0 = store
                    const int32_t disp = int32_t(((uint32_t)emulator->readByte(at + 6) << 24) |
                                                 ((uint32_t)emulator->readByte(at + 7) << 16) |
                                                 ((uint32_t)emulator->readByte(at + 8) << 8)  |
                                                  (uint32_t)emulator->readByte(at + 9));
                    address = (regs.er[ern] + disp) & 0x00FFFFFFu;
                    break;
                }
                default:
                    ok = false;
                    break;
            }

            if (!ok) {
                printf("[LDC-STC-EXT-UNKNOWN] 0x%06X: bytes 01 %02X %02X %02X - not a row in "
                       "Appendix A.2's LDC/STC block. HALTING rather than guessing.\n",
                       at, toEXR ? 0x41 : 0x40, b3, b4);
                emulator->handleIllegalInstruction();
                return true;
            }

            if (store) {
                // BUG121: the live CCR, not the shadow - NZVC and H live in the flags struct.
                const uint8_t v = toEXR ? uint8_t(regs.exr & 0xFF) : emulator->ccrByteLive();
                emulator->writeWord(address, uint16_t(uint16_t(v) << 8));   // high half - see note
                if (!g_h8s_quiet_boot)
                    printf("[EXECUTE] 0x%06X: STC.W %s,@0x%06X = 0x%02X\n",
                           at, toEXR ? "EXR" : "CCR", address, v);
            } else {
                const uint8_t v = uint8_t(emulator->readWord(address) >> 8);
                if (toEXR) regs.exr = uint16_t((regs.exr & 0xFF00) | v);   // EXR: NO condition code
                else       emulator->setCCRFromByte(v);                     // CCR: all six replaced
                if (!g_h8s_quiet_boot)
                    printf("[EXECUTE] 0x%06X: LDC.W @0x%06X,%s = 0x%02X\n",
                           at, address, toEXR ? "EXR" : "CCR", v);
            }
            emulator->addCycles(H8S_CYC_BASE_MEM);
            return true;
        }

        // 0x01F0 = the 32-bit logical group. Encoding RENDERED page 767,
        // flags RENDERED page 742 (Logical Instructions):
        //     OR.L / XOR.L / AND.L  ERs,ERd    I- H- N* Z* V=0 C-
        // V is CLEARED, not computed. C and H are untouched.
        if (instruction.opcode == 0x01 && instruction.addressing_mode == 0x12) {
            const uint8_t sub = uint8_t(instruction.source_operand);        // 0x64/0x65/0x66
            const uint8_t b4  = uint8_t(instruction.destination_operand);
            const uint8_t ers = (b4 >> 4) & 0x07;
            const uint8_t erd =  b4       & 0x07;

            const uint32_t s = regs.er[ers];
            const uint32_t d = regs.er[erd];
            const uint32_t result = (sub == 0x64) ? (d | s)
                                  : (sub == 0x65) ? (d ^ s)
                                                  : (d & s);
            emulator->setERd(erd, result);

            flags.zero     = (result == 0);
            flags.negative = (result & 0x80000000u) != 0;
            flags.overflow = false;                 // V is cleared
            // carry and half_carry: NOT TOUCHED, per the rendered page.

            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s ER%d,ER%d -> 0x%08X\n",
                   pc - instruction.size, instruction.mnemonic.c_str(), ers, erd, result);
            return true;
        }

        // 2026-09-13: a branch used to sit here claiming a "firmware-specific 0x01
        // instruction (MOV.B #imm8, Rd)" for any 0x01 decoded at size 2. There is no such
        // instruction: 0x01 is ALWAYS a prefix in Appendix A.1, and MOV.B #xx:8,Rd is
        // `F rd | IMM` (rendered page 765). It was the third fiction of the MOVA/MOVU
        // family. It read the PREFIX byte's high nibble as a register index, got 15 from
        // `01 F0`, and halted the CPU - measured as HALT-0x11E3C on `01 F0 64 22`, which
        // is OR.L ER2,ER2 and is now handled above.
        //
        // A 0x01 that reaches here is a DECODE FAILURE, so say that, and say it with the
        // bytes. Do not invent an instruction to keep the machine moving - that is how
        // this project spent months believing a firmware that was never running.
        if (instruction.opcode == 0x01 && instruction.size == 2) {
            const uint32_t start = pc - instruction.size;
            printf("[0x01-UNDECODED] PC=0x%06X bytes %02X %02X %02X %02X - 0x01 is always a "
                   "prefix; this form is not decoded. Halting rather than guessing.\n",
                   start,
                   emulator->readByte(start), emulator->readByte(start + 1),
                   emulator->readByte(start + 2), emulator->readByte(start + 3));
            emulator->halt();
            return false;
        }

        uint32_t source_value = 0;
        uint32_t dest_value = 0;

        // Get source value based on addressing mode
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        } else {
            // Memory addressing modes
            uint32_t source_address = calculateEffectiveAddress(instruction, emulator, true);
            source_value = readFromAddress(source_address, instruction.size, emulator);
            updateRegisterAfterAddressing(const_cast<H8S2350Instruction&>(instruction), emulator, true);
        }

        // Write to destination
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            setRegisterValue(regs, instruction.destination_operand, source_value, instruction.size);
        } else {
            uint32_t dest_address = calculateEffectiveAddress(instruction, emulator, false);
            writeToAddress(dest_address, source_value, instruction.size, emulator);
            updateRegisterAfterAddressing(const_cast<H8S2350Instruction&>(instruction), emulator, false);
        }

        // Update flags for move operations
        updateFlags(flags, source_value, instruction.size);

        return true;
    }
    
    bool H8S2350InstructionExecutor::executeAdd(const H8S2350Instruction& instruction, 
                                               H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t source_value = 0;
        uint32_t dest_value = 0;
        
        // Get source value
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        }
        
        // Get destination value
        dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform addition
        uint32_t result = dest_value + source_value;
        bool carry = (result < dest_value); // Check for carry
        bool overflow = false; // TODO: Implement proper overflow detection
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, carry, overflow);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeSubtract(const H8S2350Instruction& instruction, 
                                                     H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t source_value = 0;
        uint32_t dest_value = 0;
        
        // Get source value
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        }
        
        // Get destination value
        dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform subtraction
        uint32_t result = dest_value - source_value;
        bool carry = (source_value > dest_value); // Check for borrow
        bool overflow = false; // TODO: Implement proper overflow detection
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, carry, overflow);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeCompare(const H8S2350Instruction& instruction, 
                                                    H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        // Determine operand size from opcode: 0x1C=CMP.B (byte), 0x1D=CMP.W (word)
        uint8_t operand_size = (instruction.opcode == 0x1C) ? 0 : 1;
        
        uint32_t source_value = 0;
        uint32_t dest_value = 0;
        
        // Get source value
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, operand_size);
        }
        
        // Get destination value
        dest_value = getRegisterValue(regs, instruction.destination_operand, operand_size);
        
        // Perform comparison (dest - source)
        uint32_t result = dest_value - source_value;
        bool carry = (source_value > dest_value); // Check for borrow
        bool overflow = false; // TODO: Implement proper overflow detection
        
        // Update flags (don't store result)
        updateFlags(flags, result, operand_size, carry, overflow);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeAnd(const H8S2350Instruction& instruction, 
                                                H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t source_value = 0;
        uint32_t dest_value = 0;
        
        // Get source value
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        }
        
        // Get destination value
        dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform AND operation
        uint32_t result = dest_value & source_value;
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags: N/Z from result; preserve C/H/V per manual
        uint32_t mask = (instruction.size == 0) ? 0xFFu : (instruction.size == 1) ? 0xFFFFu : 0xFFFFFFFFu;
        uint32_t sign_bit = (mask + 1u) >> 1;
        flags.zero = ((result & mask) == 0);
        flags.negative = (result & sign_bit) != 0;
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeOr(const H8S2350Instruction& instruction, 
                                               H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t source_value = 0;
        uint32_t dest_value = 0;
        
        // Get source value
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        }
        
        // Get destination value
        dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform OR operation
        uint32_t result = dest_value | source_value;
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, false, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeXor(const H8S2350Instruction& instruction, 
                                                H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t source_value = 0;
        uint32_t dest_value = 0;
        
        // Get source value
        if (instruction.addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            source_value = instruction.immediate_value;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            source_value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        }
        
        // Get destination value
        dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform XOR operation
        uint32_t result = dest_value ^ source_value;
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, false, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeNot(const H8S2350Instruction& instruction, 
                                                H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform NOT operation
        uint32_t result = ~dest_value;
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, false, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeShiftLeft(const H8S2350Instruction& instruction, 
                                                      H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform shift left (logical)
        uint32_t result = dest_value << 1;
        bool carry = (dest_value & 0x80000000) != 0; // MSB becomes carry
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, carry, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeShiftRight(const H8S2350Instruction& instruction, 
                                                       H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform shift right (logical)
        uint32_t result = dest_value >> 1;
        bool carry = (dest_value & 0x01) != 0; // LSB becomes carry
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, carry, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeRotateLeft(const H8S2350Instruction& instruction, 
                                                       H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform rotate left
        bool msb = (dest_value & 0x80000000) != 0;
        uint32_t result = (dest_value << 1) | (msb ? 1 : 0);
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, msb, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeRotateRight(const H8S2350Instruction& instruction, 
                                                        H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t dest_value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Perform rotate right
        bool lsb = (dest_value & 0x01) != 0;
        uint32_t result = (dest_value >> 1) | (lsb ? 0x80000000 : 0);
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, lsb, false);
        
        return true;
    }
    
    // Bcc d:8 conditional branch (0x40-0x4F). Condition code in source_operand (0..F).
    //   Renesas p.279/280, cross-checked with cybemu disassembler.
    bool H8S2350InstructionExecutor::executeBcc8(const H8S2350Instruction& instruction,
                                                 H8S2350Emulator* emulator)
    {
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();   // already advanced past the 2-byte insn
        uint32_t base = pc - instruction.size;
        int8_t disp = (int8_t)emulator->readByte(base + 1);   // signed 8-bit displacement
        uint8_t cc = (uint8_t)instruction.source_operand & 0x0F;
        bool C = flags.carry, Z = flags.zero, N = flags.negative, V = flags.overflow;
        bool take = false;
        switch (cc) {
            case 0x0: take = true; break;                 // BRA (always)
            case 0x1: take = false; break;                // BRN (never)
            case 0x2: take = !C && !Z; break;             // BHI
            case 0x3: take = C || Z; break;               // BLS
            case 0x4: take = !C; break;                   // BCC (BHS)
            case 0x5: take = C; break;                    // BCS (BLO)
            case 0x6: take = !Z; break;                   // BNE
            case 0x7: take = Z; break;                    // BEQ
            case 0x8: take = !V; break;                   // BVC
            case 0x9: take = V; break;                    // BVS
            case 0xA: take = !N; break;                   // BPL
            case 0xB: take = N; break;                    // BMI
            case 0xC: take = (N == V); break;             // BGE
            case 0xD: take = (N != V); break;             // BLT
            case 0xE: take = !Z && (N == V); break;       // BGT
            case 0xF: take = Z || (N != V); break;        // BLE
        }
        static const char* nm[16] = {"BRA","BRN","BHI","BLS","BCC","BCS","BNE","BEQ",
                                      "BVC","BVS","BPL","BMI","BGE","BLT","BGT","BLE"};
        if (take) {
            uint32_t target = (base + 2 + disp) & 0x00FFFFFF;   // PC-relative from end of insn
            emulator->setProgramCounter(target);
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s %+d -> TAKEN, PC=0x%06X\n", base, nm[cc], (int)disp, target);
        } else {
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s %+d -> not taken\n", base, nm[cc], (int)disp);
        }
        return true;
    }

    // 0x58 = Bcc d:16 (16-bit displacement conditional branch) - Renesas p.279/280.
    bool H8S2350InstructionExecutor::executeBcc16(const H8S2350Instruction& instruction,
                                                  H8S2350Emulator* emulator)
    {
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        int16_t disp = (int16_t)(((uint16_t)emulator->readByte(base + 2) << 8)
                                 | emulator->readByte(base + 3));   // signed 16-bit displacement
        uint8_t cc = (uint8_t)((emulator->readByte(base + 1) >> 4) & 0x0F);
        bool C = flags.carry, Z = flags.zero, N = flags.negative, V = flags.overflow;
        bool take = false;
        switch (cc) {
            case 0x0: take = true; break;                 // BRA
            case 0x1: take = false; break;                // BRN
            case 0x2: take = !C && !Z; break;             // BHI
            case 0x3: take = C || Z; break;               // BLS
            case 0x4: take = !C; break;                   // BCC (BHS)
            case 0x5: take = C; break;                    // BCS (BLO)
            case 0x6: take = !Z; break;                   // BNE
            case 0x7: take = Z; break;                    // BEQ
            case 0x8: take = !V; break;                   // BVC
            case 0x9: take = V; break;                    // BVS
            case 0xA: take = !N; break;                   // BPL
            case 0xB: take = N; break;                    // BMI
            case 0xC: take = (N == V); break;             // BGE
            case 0xD: take = (N != V); break;             // BLT
            case 0xE: take = !Z && (N == V); break;       // BGT
            case 0xF: take = Z || (N != V); break;        // BLE
        }
        static const char* nm[16] = {"BRA16","BRN16","BHI16","BLS16","BCC16","BCS16","BNE16","BEQ16",
                                      "BVC16","BVS16","","", "", "BLT16","BGT16","BLE16"};
        if (take) {
            uint32_t target = (base + 4 + disp) & 0x00FFFFFF;   // PC-relative from end of 4-byte insn
            emulator->setProgramCounter(target);
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s %+d -> TAKEN, PC=0x%06X\n", base, nm[cc], (int)disp, target);
        } else {
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s %+d -> not taken\n", base, nm[cc], (int)disp);
        }
        return true;
    }

    // 0x1B = DEC.W/DEC.L/SUBS group (Renesas H8S/2600 p.115/253/283)
    //   1B 0 0erd / 1B 5 rd / 1B 7 0erd / 1B 8 0erd / 1B 9 0erd / 1B D rd / 1B F 0erd
    bool H8S2350InstructionExecutor::execute0x1BInstruction(const H8S2350Instruction& instruction,
                                                            H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        if (!g_h8s_quiet_boot) printf("[EXECUTE-0x1B-ENTRY] pc=0x%06X base=0x%06X opcode=0x%04X size=%d src_op=%d dst_op=%d\n",
               pc, base, (unsigned)instruction.opcode, (int)instruction.size,
               (int)instruction.source_operand, (int)instruction.destination_operand);
        uint8_t hi = (uint8_t)instruction.source_operand & 0x0F;       // op2 high nibble (sub-op)
        uint8_t lo = (uint8_t)instruction.destination_operand & 0x0F;  // op2 low nibble
        uint8_t reg_idx = lo & 0x07;                                    // 3-bit reg

        switch (hi) {
            // BUG35 - DEC.W's operand is the FOUR-bit 16-bit register field, not three.
            // Renesas HM Rev 3.00, Appendix A.2 legend, RENDERED page 771:
            //     16-Bit Register:  0000-0111 -> R0..R7     1000-1111 -> E0..E7
            // `lo & 0x07` decremented R2 where the firmware wrote E2. Measured at
            // 0x01CE1E, `1B 5A` = DEC.W #1,E2: the counter never moved, because the
            // loop's own `6D 02` (MOV.W @ER0+,R2) overwrote the register we were using
            // as the counter. DEC.L and SUBS below keep 3 bits - their field is
            // `0:erd`, which is correct for them.
            case 0x5: {  // DEC.W #1, Rd/En
                const uint8_t wreg = lo & 0x0F;
                uint16_t before = (uint16_t)getRegisterValue(regs, wreg, 1);
                uint16_t after  = before - 1;
                setRegisterValue(regs, wreg, after, 1);
                if ((wreg & 0x07) == 7) emulator->syncRegAfterLongWrite(7);
                flags.zero = (after == 0);
                flags.negative = (after & 0x8000) != 0;
                bool sa = (before & 0x8000) != 0;
                bool sr = (after  & 0x8000) != 0;
                // BUG121: this was INVERTED - it set V for H'0000-1 and never for H'8000-1.
                // REJ09B0139 RENDERED p.99 (DEC.W) Notes: H'8000-1, H'8000-2, H'8001-2 overflow.
                flags.overflow = sa && !sr;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: DEC.W #1, %s%d  (0x%04X -> 0x%04X, Z=%d)\n",
                       base, (wreg & 0x08) ? "E" : "R", wreg & 0x07, before, after, flags.zero ? 1 : 0);
                break;
            }
            case 0xD: {  // DEC.W #2, Rd/En
                const uint8_t wreg = lo & 0x0F;
                uint16_t before = (uint16_t)getRegisterValue(regs, wreg, 1);
                uint16_t after  = before - 2;
                setRegisterValue(regs, wreg, after, 1);
                if ((wreg & 0x07) == 7) emulator->syncRegAfterLongWrite(7);
                flags.zero = (after == 0);
                flags.negative = (after & 0x8000) != 0;
                flags.overflow = ((before & ~after) & 0x8000u) != 0;   // BUG121: V was never written (p.99)
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: DEC.W #2, %s%d  (0x%04X -> 0x%04X)\n",
                       base, (wreg & 0x08) ? "E" : "R", wreg & 0x07, before, after);
                break;
            }
            case 0x7: {  // DEC.L #1, ERd
                uint32_t before = regs.er[reg_idx];
                uint32_t after  = before - 1;
                regs.er[reg_idx] = after;
                emulator->syncRegAfterLongWrite(reg_idx);
                flags.zero = (after == 0);
                flags.negative = (after & 0x80000000u) != 0;
                flags.overflow = ((before & ~after) & 0x80000000u) != 0;   // BUG121: V was never written (REJ09B0139 RENDERED p.100)
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: DEC.L #1, ER%d  (0x%08X -> 0x%08X)\n", base, reg_idx, before, after);
                break;
            }
            case 0xF: {  // DEC.L #2, ERd
                uint32_t before = regs.er[reg_idx];
                uint32_t after  = before - 2;
                regs.er[reg_idx] = after;
                emulator->syncRegAfterLongWrite(reg_idx);
                flags.zero = (after == 0);
                flags.negative = (after & 0x80000000u) != 0;
                flags.overflow = ((before & ~after) & 0x80000000u) != 0;   // BUG121: V was never written (p.100)
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: DEC.L #2, ER%d  (0x%08X -> 0x%08X)\n", base, reg_idx, before, after);
                break;
            }
            case 0x0:    // SUBS #1, ERd  (does NOT affect flags per Renesas)
            case 0x8:    // SUBS #2, ERd
            case 0x9: {  // SUBS #4, ERd
                uint32_t sub = (hi == 0x0) ? 1u : (hi == 0x8) ? 2u : 4u;
                uint32_t before = regs.er[reg_idx];
                uint32_t after  = before - sub;
                regs.er[reg_idx] = after;
                emulator->syncRegAfterLongWrite(reg_idx);
                // SUBS does not change CCR flags (p.253)
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: SUBS #%u, ER%d  (0x%08X -> 0x%08X)\n", base, sub, reg_idx, before, after);
                break;
            }
            default:
                printf("[ERROR] 0x1B unknown sub-op 0x%X at PC 0x%06X\n", hi, base);
                emulator->handleIllegalInstruction();
                return true;
        }
        return true;
    }

    bool H8S2350InstructionExecutor::executeBranch(const H8S2350Instruction& instruction, 
                                                   H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        
        // FORENSICS: Capture state at branch decision point
        uint64_t state_hash = MS2000::computeStateHash(*emulator);
        printf("[FORENSICS] Branch decision at PC=0x%06X opcode=0x%04X hash=%016" PRIx64 "\n", 
               pc, instruction.opcode & 0xFFFF, state_hash);
        
        bool should_branch = false;
        
        // Check condition based on opcode
        switch (instruction.opcode) {
            case H8S2350Opcode::BRA: should_branch = true; break;
            case H8S2350Opcode::BRN: should_branch = false; break;
            case H8S2350Opcode::BHI: should_branch = !flags.carry && !flags.zero; break;
            case H8S2350Opcode::BLS: should_branch = flags.carry || flags.zero; break;
            case H8S2350Opcode::BCC: should_branch = !flags.carry; break;
            case H8S2350Opcode::BCS: should_branch = flags.carry; break;
            case H8S2350Opcode::BNE: should_branch = !flags.zero; break;
            case H8S2350Opcode::BEQ: should_branch = flags.zero; break;
            case H8S2350Opcode::BVC: should_branch = !flags.overflow; break;
            case H8S2350Opcode::BVS: should_branch = flags.overflow; break;
            case H8S2350Opcode::BPL: should_branch = !flags.negative; break;
            case H8S2350Opcode::BMI: should_branch = flags.negative; break;
            case H8S2350Opcode::BGE: should_branch = (flags.negative == flags.overflow); break;
            case H8S2350Opcode::BLT: should_branch = (flags.negative != flags.overflow); break;
            case H8S2350Opcode::BGT: should_branch = !flags.zero && (flags.negative == flags.overflow); break;
            case H8S2350Opcode::BLE: should_branch = flags.zero || (flags.negative != flags.overflow); break;
            
            // Extended 16-bit displacement branches (same condition logic)
            case 0x006F: case 0x0070: // BRA_EXT, BRA16
                should_branch = true; break;
            case 0x0071: // BRN16
                should_branch = false; break;
            case 0x0072: // BHI16
                should_branch = !flags.carry && !flags.zero; break;
            case 0x0073: // BLS16  
                should_branch = flags.carry || flags.zero; break;
            case 0x0074: // BCC16
                should_branch = !flags.carry; break;
            case 0x0075: // BCS16
                should_branch = flags.carry; break;
            case 0x0076: // BNE16
                should_branch = !flags.zero; break;
            case 0x0077: // BEQ16
                should_branch = flags.zero; break;
            case 0x0078: // BVC16
                should_branch = !flags.overflow; break;
            case 0x0079: // BVS16
                should_branch = flags.overflow; break;
            // NOTE: 0x7A-0x7F are NOT extended branches in H8S ISA
            // They are bit-manipulation groups handled by executeBitManipGroup()
            // This fallback should NEVER be reached for 0x007C-0x007F
            // If it is, it indicates a decoder regression
        }
        
        if (!g_h8s_quiet_boot) printf("[DEBUG-BRANCH] Instruction opcode=0x%04X, should_branch=%d, Z=%d N=%d V=%d\n", 
               instruction.opcode, should_branch, flags.zero, flags.negative, flags.overflow);
        
        if (should_branch) {
            // Standard 8-bit displacement branch.
            // FIX27c: the fictional "extended branch" path (opcode 0x006F-0x007F, reading
            // its own operand bytes as a 16-bit displacement - the BLE16 teleport) is
            // DELETED. Real Bcc d:16 = 0x58 [cc]0 disp16, handled in its own decoder path.
            int16_t offset = static_cast<int16_t>(instruction.displacement);
            regs.pc = (regs.pc + offset) & 0x00FFFFFF;  // ins04.txt: PC always 24-bit
            if (!g_h8s_quiet_boot) printf("[BRANCH] 8-bit branch taken: PC=0x%06X + disp=%d -> 0x%06X\n",
                   pc - instruction.size, offset, regs.pc);
        } else {
            if (!g_h8s_quiet_boot) printf("[DEBUG-BRANCH] Branch NOT taken (condition false)\n");
        }
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeJump(const H8S2350Instruction& instruction, 
                                                 H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        
        // Set PC to target address (ins04.txt: PC always 24-bit)
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            regs.pc = regs.r[instruction.source_operand] & 0x00FFFFFF;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::ABSOLUTE_ADDRESS) {
            regs.pc = instruction.immediate_value & 0x00FFFFFF;
        }
        
        return true;
    }

    // 0x5A = JMP @aa:24 (24-bit absolute jump) - Renesas. Reads target directly from ROM.
    bool H8S2350InstructionExecutor::executeJMP_AA24(const H8S2350Instruction& instruction,
                                                     class H8S2350Emulator* emulator)
    {
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint32_t target = ((uint32_t)emulator->readByte(base + 1) << 16)
                        | ((uint32_t)emulator->readByte(base + 2) << 8)
                        |  (uint32_t)emulator->readByte(base + 3);
        target &= 0x00FFFFFF;
        emulator->setProgramCounter(target);
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: JMP @0x%06X\n", base, target);
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeJumpSubroutine(const H8S2350Instruction& instruction, 
                                                           H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        
        // Push return address onto stack (24-bit address, 3 bytes)
        if (!g_h8s_quiet_boot) printf("[DEBUG-JSR] Pushing return address 0x%06X to stack, SP=0x%06X\n", regs.pc, regs.sp);
        emulator->push24(regs.pc);
        
        // Set PC to target address (ins04.txt: PC always 24-bit)
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            regs.pc = regs.r[instruction.source_operand] & 0x00FFFFFF;
        } else if (instruction.addressing_mode == H8S2350AddressingMode::ABSOLUTE_ADDRESS) {
            regs.pc = instruction.immediate_value & 0x00FFFFFF;
        }
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executePush(const H8S2350Instruction& instruction, 
                                                 H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        
        uint32_t value = getRegisterValue(regs, instruction.source_operand, instruction.size);
        
        // Push value onto stack using size-appropriate aligned operations
        switch (instruction.size) {
            case 0: // Byte
                emulator->pushByte(value & 0xFF);
                break;
            case 1: // Word
                emulator->pushWordAligned(value & 0xFFFF);
                break;
            case 2: // Long
                emulator->pushLongAligned(value);
                break;
        }
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executePop(const H8S2350Instruction& instruction, 
                                                H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        
        uint32_t value = 0;
        
        // Pop value from stack using size-appropriate aligned operations
        switch (instruction.size) {
            case 0: // Byte
                value = emulator->popByte();
                break;
            case 1: // Word
                value = emulator->popWordAligned();
                break;
            case 2: // Long
                value = emulator->popLongAligned();
                break;
        }
        
        // Store in destination register
        setRegisterValue(regs, instruction.destination_operand, value, instruction.size);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeNop(const H8S2350Instruction& instruction,
                                                 H8S2350Emulator* emulator)
    {
        // NOP does nothing, just advance PC
        uint32_t pc = emulator->getProgramCounter();
        // Diagnostic: log only when we are inside the boot NOP-pad region (0xC22-0xC85)
        if (pc - instruction.size >= 0xC22 && pc - instruction.size <= 0xC86) {
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: NOP (size=%d, opcode=0x%04X)\n",
                   pc - instruction.size, (int)instruction.size, (unsigned)instruction.opcode);
        }
        return true;
    }

    bool H8S2350InstructionExecutor::executeNopFF(const H8S2350Instruction& instruction,
                                                  H8S2350Emulator* emulator)
    {
        // NOP_FF (0xFFFF) - firmware padding, treat as NOP
        // This handles consecutive 0xFF bytes in firmware that would otherwise be illegal instructions
        // No debug output needed for NOP operations
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeSleep(const H8S2350Instruction& instruction, 
                                                  H8S2350Emulator* emulator)
    {
        // SLEEP puts the CPU in sleep mode
        emulator->halt();
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeTrap(const H8S2350Instruction& instruction, 
                                                 H8S2350Emulator* emulator)
    {
        uint8_t imm = instruction.immediate_value & 0x03;  // ins03.txt: only #0..#3
        return emulator->executeTrap(imm);
    }
    
    // ==== Additional Instructions Implementation ====
    
    bool H8S2350InstructionExecutor::executeIncrement(const H8S2350Instruction& instruction, 
                                                     H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t value = 0;
        uint32_t address = 0;
        bool is_memory = false;
        
        // Get current value
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        } else {
            address = calculateEffectiveAddress(instruction, emulator, false);
            value = readFromAddress(address, instruction.size, emulator);
            is_memory = true;
        }
        
        // Increment value
        uint32_t result = value + 1;
        bool overflow = (result < value); // Check for overflow
        
        // Store result
        if (is_memory) {
            writeToAddress(address, result, instruction.size, emulator);
        } else {
            setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        }
        
        // Update flags
        updateFlags(flags, result, instruction.size, false, overflow);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeDecrement(const H8S2350Instruction& instruction, 
                                                     H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t value = 0;
        uint32_t address = 0;
        bool is_memory = false;
        
        // Get current value
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        } else {
            address = calculateEffectiveAddress(instruction, emulator, false);
            value = readFromAddress(address, instruction.size, emulator);
            is_memory = true;
        }
        
        // Decrement value
        uint32_t result = value - 1;
        bool overflow = (result > value); // Check for underflow
        
        // Store result
        if (is_memory) {
            writeToAddress(address, result, instruction.size, emulator);
        } else {
            setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        }
        
        // Update flags
        updateFlags(flags, result, instruction.size, false, overflow);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeClear(const H8S2350Instruction& instruction, 
                                                 H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t address = 0;
        bool is_memory = false;
        
        // Determine target
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            setRegisterValue(regs, instruction.destination_operand, 0, instruction.size);
        } else {
            address = calculateEffectiveAddress(instruction, emulator, false);
            writeToAddress(address, 0, instruction.size, emulator);
            is_memory = true;
        }
        
        // Update flags (result is always 0)
        updateFlags(flags, 0, instruction.size, false, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeTest(const H8S2350Instruction& instruction, 
                                                H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t value = 0;
        
        // Get value to test
        if (instruction.addressing_mode == H8S2350AddressingMode::REGISTER_DIRECT) {
            value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        } else {
            uint32_t address = calculateEffectiveAddress(instruction, emulator, false);
            value = readFromAddress(address, instruction.size, emulator);
        }
        
        // Test operation (AND with itself)
        uint32_t result = value & value;
        
        // Update flags (don't store result)
        updateFlags(flags, result, instruction.size, false, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeExtend(const H8S2350Instruction& instruction, 
                                                  H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t value = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        uint32_t result = 0;
        
        // Extend operation
        if (instruction.opcode == H8S2350Opcode::EXT_B) {
            // Sign extend byte to word
            result = static_cast<int8_t>(value & 0xFF);
        }
        // BUG37: the EXT_W / EXTU_B / EXTU_W branches that stood here were keyed on
        // 0x64 / 0x65 / 0x66, which are OR.W / XOR.W / AND.W Rs,Rd (RENDERED page 806).
        // They are deleted with their constants. The real EXTS/EXTU are 0x17-prefixed
        // forms and are not implemented at all - when the firmware reaches one it will
        // now be reported as an unknown opcode rather than silently sign-extending.
        
        // Store result (always in register)
        setRegisterValue(regs, instruction.destination_operand, result, instruction.size);
        
        // Update flags
        updateFlags(flags, result, instruction.size, false, false);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeExchange(const H8S2350Instruction& instruction, 
                                                    H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        
        uint32_t value1 = getRegisterValue(regs, instruction.source_operand, instruction.size);
        uint32_t value2 = getRegisterValue(regs, instruction.destination_operand, instruction.size);
        
        // Exchange values
        setRegisterValue(regs, instruction.source_operand, value2, instruction.size);
        setRegisterValue(regs, instruction.destination_operand, value1, instruction.size);
        
        return true;
    }
    
    bool H8S2350InstructionExecutor::executeSwap(const H8S2350Instruction& instruction, 
                                                H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        uint32_t value = getRegisterValue(regs, instruction.destination_operand, 1); // Word size
        
        // Swap bytes in word
        uint32_t result = ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
        
        // Store result
        setRegisterValue(regs, instruction.destination_operand, result, 1);
        
        // Update flags
        updateFlags(flags, result, 1, false, false);
        
        return true;
    }

    // GPT5 fw29.txt helper methods implementation
    uint8_t H8S2350InstructionDecoder::calculateInstructionSize(H8S2350Emulator& emulator, uint32_t pc, uint16_t instruction)
    {
        // Basic size calculation - can be enhanced based on addressing modes
        uint8_t opcode = extractOpcode(instruction);
        uint8_t addressing_mode = extractAddressingMode(instruction);
        
        // Most H8S instructions are 2 bytes, some are 4 or 6
        if (addressing_mode == H8S2350AddressingMode::ABSOLUTE_ADDRESS ||
            addressing_mode == H8S2350AddressingMode::REGISTER_INDIRECT_DISP) {
            return 4; // Instructions with displacement/absolute addressing
        } else if (addressing_mode == H8S2350AddressingMode::IMMEDIATE) {
            // Immediate instructions vary by operand size
            return 4; // Most immediate instructions are 4 bytes
        }
        
        return 2; // Default instruction size
    }
    
    uint32_t H8S2350InstructionDecoder::calculateBaseCycles(uint8_t opcode, uint8_t addressing_mode)
    {
        // Basic cycle calculation - can be refined based on instruction type
        uint32_t cycles = 1;
        
        // Memory access adds cycles
        if (addressing_mode == H8S2350AddressingMode::REGISTER_INDIRECT ||
            addressing_mode == H8S2350AddressingMode::ABSOLUTE_ADDRESS ||
            addressing_mode == H8S2350AddressingMode::REGISTER_INDIRECT_DISP) {
            cycles += 1;
        }
        
        // Complex instructions take more cycles
        // BUG38: this used to read `opcode >= ADD_B && opcode <= SUB_L`, i.e. 0x03..0x08,
        // a range built out of fictional constants. It is a cycle-count heuristic only,
        // so the literal range is kept unchanged in value and no longer cites them.
        if (opcode >= 0x03 && opcode <= H8S2350Opcode::SUB_L) {
            cycles += 1; // Arithmetic operations
        } else if (opcode >= H8S2350Opcode::SHAL_B && opcode <= H8S2350Opcode::ROTXR_L) {
            cycles += 2; // Shift/rotate operations
        }
        
        return cycles;
    }
    
    // New executor method implementation
    ExecResult H8S2350InstructionExecutor::execute(H8S2350Emulator& emulator, const H8S2350Instruction& instruction, uint32_t pc0)
    {
        // Arm PC fuse to enforce decoded_pc+size through to next fetch
        const bool watch = is_shiftrot_or_shar_primary(instruction.opcode);
        const uint32_t want_pc = pcMask24(instruction.decoded_pc + instruction.size);
#if defined(_DEBUG) || defined(H8S_ENFORCE_SHIFTROT_PC)
        if (watch) { emulator.armPCFuse(instruction.opcode, want_pc); }
#endif

        // RAII guard to enforce PC at function exit for shift/rotate/SHAR
        struct PcGuard {
            H8S2350Emulator& emu; uint32_t want; const char* tag; bool active;
            ~PcGuard(){
#if defined(_DEBUG) || defined(H8S_ENFORCE_SHIFTROT_PC)
                if (active && emu.getRegisters().pc != want) {
                    printf("[PCGUARD-%s] late fix: had=%06X want=%06X\n", tag, emu.getRegisters().pc, want);
                    emu.setProgramCounter(want);
                }
#endif
            }
        } guard{ emulator, want_pc, "WRAP", watch };

    // Call legacy execute method for now
    bool success = execute(instruction, &emulator);

    // Enforce PC for shift/rotate/SHAR primaries at the common return site (Debug-safe)
    if (watch) {
        if (emulator.getRegisters().pc != want_pc) {
            emulator.setProgramCounter(want_pc);
        }
#if defined(SHIFT_SMOKE_TRACE)
        printf("[AFTER-PC] op=%02X pc=%06X want=%06X start=%06X size=%u\n",
               instruction.opcode,
               emulator.getRegisters().pc,
               want_pc,
               instruction.decoded_pc,
               (unsigned)instruction.size);
#endif
    }

    // Stable post-execute snapshot for tests (exact instruction that just ran)
    emulator.updateLastExec(instruction.decoded_pc,
                            instruction.size,
                            emulator.getProgramCounter(),
                            instruction.opcode);

    // Check if instruction modified PC (branch/jump detection)
    bool pc_overridden = (emulator.getRegisters().pc != pc0 + instruction.size);

    // Avoid double-counting cycles for SHIFT/ROT/SHAR paths that already accounted cycles via done()
    const uint32_t cycles_to_add = watch ? 0u : instruction.baseCycles;

    if (pc_overridden) {
        return ExecResult(emulator.getRegisters().pc, cycles_to_add);
    } else {
        return ExecResult(cycles_to_add);
    }
    }
    
    bool H8S2350InstructionExecutor::executeReturnFromSubroutine(const H8S2350Instruction& instruction, 
                                                               H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        
        // Debug: Show stack state before RTS
        if (!g_h8s_quiet_boot) printf("[DEBUG-RTS] SP=0x%06X before pop\n", regs.sp);

        // 2026-09-13, STACK-0x1CE6E: report the FIRST RTS unconditionally, with ER7 as the
        // executor sees it. The PC ring shows ER7 = 0xFFFBFC at this instruction's boundary
        // while pop24 then reads 0xFFFC00, so ER7 moves between the two. Printing it here
        // splits that window in half: if it is already 0xFFFC00 the change happened before
        // the executor ran (decode or dispatch); if it is still 0xFFFBFC, it happened inside
        // pop24's own preamble.
        {
            static bool first_rts = true;
            if (first_rts) {
                first_rts = false;
                printf("[RTS-ENTRY] first RTS at PC 0x%06X: ER7=0x%08X sp=0x%08X\n",
                       regs.pc & 0x00FFFFFFu, regs.er[7], regs.sp);
            }
        }
        
        // RTS: Pop return address from stack using 24-bit method
        uint32_t return_address = emulator->pop24();  // ins04.txt: PC always 24-bit
        regs.pc = return_address;
        
        if (!g_h8s_quiet_boot) printf("[DEBUG-RTS] New PC=0x%06X after RTS (from stack), SP now=0x%06X\n", return_address, regs.sp);
        
        return true;
    }
    
    // Renesas H8S/2350 Appendix A: 0x7A @aa:32 byte/word MOV group.
    bool H8S2350InstructionExecutor::execute0x7AGroup(const H8S2350Instruction& instruction,
                                                       H8S2350Emulator* emulator)
    {
        uint32_t pc = emulator->getProgramCounter();
        // Temporary debug - can be changed to H8S_TRACE later
        if (!g_h8s_quiet_boot) printf("[0x7A-EXEC] đźŽŻ Execute0x7AGroup called! PC=0x%06X\n", pc);
        
        if (instruction.size != 6) {
            printf("[H8S-7A][ERROR] Expected 0x7A ABS32 size=6, got %d at PC=%06X\n", instruction.size, pc);
            return false;
        }
        
        auto& regs = emulator->getRegisters();

        uint8_t sub = emulator->readByte(pc - instruction.size + 1);  // Second byte
        const uint8_t op = (sub >> 4) & 0x0F;      // High nibble = operation
        const uint8_t rr = sub & 0x07;             // Low nibble = register index (masked to 0-7)

        // ===================================================================
        // BUG49, 2026-09-13 - `CMP.L #xx:32,ERd` WAS REFUSED, AND THAT LEFT THE
        // WHOLE 512 KB OF DRAM UNINITIALISED.
        //
        // What stood here rejected every op except 0, 1, 4 and 5, on the claim
        // that "Appendix A assigns 0x7A to byte/word absolute MOV forms". It
        // does not. 0x7A is the LONGWORD IMMEDIATE ALU group, and the sub-byte's
        // high nibble selects the operation:
        //
        //   7A 0 0:erd | IMM32   MOV.L #xx:32,ERd   RENDERED page 802
        //   7A 1 0:erd | IMM32   ADD.L #xx:32,ERd   (see the note below)
        //   7A 2 0:erd | IMM32   CMP.L #xx:32,ERd   RENDERED page 799
        //   7A 3 0:erd | IMM32   SUB.L #xx:32,ERd   RENDERED page 806
        //   7A 4 0:erd | IMM32   OR.L  #xx:32,ERd   RENDERED page 803
        //   7A 5 0:erd | IMM32   XOR.L #xx:32,ERd   RENDERED page 806
        //   7A 6 0:erd | IMM32   AND.L #xx:32,ERd   RENDERED page 794
        //
        // Six of the seven rows are read off rendered pages. Op 1 is the only
        // slot left and ADD.L #xx:32,ERd the only remaining L-immediate
        // instruction, so it is INFERRED, not rendered - said plainly so the
        // next reader knows which line to check first if ADD.L misbehaves.
        //
        // HOW IT WAS CAUGHT, and the reach of it. The firmware's DRAM clear:
        //
        //   002020: 7A 00 00 40 00 00   MOV.L #0x00400000, ER0
        //   002026: 1A 91               SUB.L ER1,ER1
        //   002028: 01 00 69 81         MOV.L ER1,@ER0        zero four bytes
        //   00202C: 0B 90               ADDS #4, ER0
        //   00202E: 7A 20 00 48 00 00   CMP.L #0x00480000, ER0
        //   002034: 45 F2               BCS -> 0x002028
        //
        // 131,072 iterations that zero the entire V53C16256LK. Measured with
        // MS2K_LOOPWATCH=0x2034: ONE visit, ER0 = 0x00400004, CCR = 0x00.
        // 0x400004 - 0x480000 borrows, so C must be 1 and the branch must be
        // taken - but CMP.L returned false without touching a flag, so the loop
        // ran exactly once and FOUR BYTES of DRAM were cleared.
        //
        // Everything downstream follows. The firmware then reads its own
        // variables out of uninitialised memory: the MIDI-out FIFO count at
        // 0x400C3E came back as 0xFFF1, which is why LOOP-0x2F60 waits forever
        // for it to fall below 0x0400. On the real machine DRAM powers up
        // arbitrary too - the difference is that on the real machine this loop
        // runs.
        //
        // A REFUSAL IS NOT A NO-OP EITHER. `return false` left the PC advanced
        // and the flags untouched, so the machine carried on with a stale CCR -
        // the same shape as the silent fall-throughs, wearing an error message.
        // ===================================================================

        H8S_INFO_CTX(emulator, "7A", "pc=%06X sub=%02X op=%d r=%d size=%d", 
                     pc - instruction.size, sub, op, rr, instruction.size);

        const uint32_t aa32 = ((uint32_t)emulator->readByte(pc - instruction.size + 2) << 24) |
                              ((uint32_t)emulator->readByte(pc - instruction.size + 3) << 16) |
                              ((uint32_t)emulator->readByte(pc - instruction.size + 4) << 8) |
                              ((uint32_t)emulator->readByte(pc - instruction.size + 5));

        // Sprint 1.1c: Add base cycle accounting for 0x7A @aa:32 operations
        emulator->addCycles(H8S_CYC_BASE_MEM);

        // Flags for this group, from the rendered flag tables: MOV/OR/XOR/AND set
        // N and Z, clear V and leave C and H alone; ADD/CMP/SUB set H, N, Z, V and C.
        // CMP does NOT write the result back - that is its whole point.
        {
            const uint32_t a   = regs.er[rr];
            const uint32_t imm = aa32;
            auto& f = emulator->getFlags();
            uint32_t res = 0;
            bool writeback = true, arith = false, isSub = false;
            const char* nm = "?";
            switch (op) {
                case 0x0: res = imm;      nm = "MOV.L"; break;
                case 0x1: res = a + imm;  nm = "ADD.L"; arith = true; break;
                case 0x2: res = a - imm;  nm = "CMP.L"; arith = true; isSub = true; writeback = false; break;
                case 0x3: res = a - imm;  nm = "SUB.L"; arith = true; isSub = true; break;
                case 0x4: res = a | imm;  nm = "OR.L";  break;
                case 0x5: res = a ^ imm;  nm = "XOR.L"; break;
                case 0x6: res = a & imm;  nm = "AND.L"; break;
                default:
                    printf("[7A-UNKNOWN] 0x%06X: 7A %02X - sub-op %d is not in the longword immediate "
                           "group (RENDERED pages 794/799/802/803/806). Halting rather than guessing.\n",
                           pc - instruction.size, sub, op);
                    emulator->halt();
                    return false;
            }

            f.zero     = (res == 0);
            f.negative = (res & 0x80000000u) != 0;
            if (arith) {
                const bool sa = (a   & 0x80000000u) != 0;
                const bool sb = (imm & 0x80000000u) != 0;
                const bool sr = (res & 0x80000000u) != 0;
                if (isSub) {
                    f.carry    = (a < imm);                 // borrow
                    f.overflow = (sa != sb) && (sr != sa);
                    f.half_carry = ((a ^ imm ^ res) & 0x10000000u) != 0;   // BUG121: carry/borrow OUT of bit 27 = into bit 28 (REJ09B0139 RENDERED p.50/93/236)
                } else {
                    f.carry    = (uint64_t(a) + uint64_t(imm)) > 0xFFFFFFFFull;
                    f.overflow = (sa == sb) && (sr != sa);
                    f.half_carry = ((a ^ imm ^ res) & 0x10000000u) != 0;   // BUG121: carry/borrow OUT of bit 27 = into bit 28 (REJ09B0139 RENDERED p.50/93/236)
                }
            } else {
                f.overflow = false;                          // C and H untouched
            }

            if (writeback) emulator->setERd(rr, res);        // alias-safe: keeps SP in step

            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s #0x%08X, ER%d -> 0x%08X (C=%d Z=%d N=%d)\n",
                   pc - instruction.size, nm, imm, rr, res,
                   f.carry ? 1 : 0, f.zero ? 1 : 0, f.negative ? 1 : 0);
        }
        
        return true;
    }


    bool H8S2350InstructionExecutor::executeReturnFromException(const H8S2350Instruction& instruction,
                                                               H8S2350Emulator* emulator)
    {
        return emulator->executeRTE();
    }

    // Handle 0xF9 instruction (MOV.B Rs, Rd) - boot2.txt: Unified PC advancement
    bool H8S2350InstructionExecutor::execute0xF9Instruction(const H8S2350Instruction& instruction,
                                                            H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // MOV.B #xx:8, Rd  =  0xFr IMM  (Renesas H8S/2600 p.154/284)
        // The opcode low-nibble 'r' selects the 8-bit destination register:
        //   r = 0..7  -> R0H..R7H (upper byte)
        //   r = 8..15 -> R0L..R7L (lower byte)
        // The 2nd byte is the 8-bit immediate value.
        uint8_t opcodeByte = emulator->readByte(pc - instruction.size);     // 0xF0..0xFF
        uint8_t imm8       = emulator->readByte(pc - instruction.size + 1); // immediate
        uint8_t rnibble    = opcodeByte & 0x0F;
        uint8_t regIdx     = rnibble & 0x07;
        bool    isHigh     = (rnibble < 8);  // 0..7 = high byte, 8..15 = low byte

        if (isHigh) {
            regs.rh[regIdx] = imm8;
        } else {
            regs.rl[regIdx] = imm8;
        }
        emulator->syncRegAfterByteWrite(regIdx, isHigh);  // alias-safe (keeps ER/SP intact)

        flags.zero = (imm8 == 0);
        flags.negative = (imm8 & 0x80) != 0;
        flags.overflow = false;  // MOV clears V, leaves C

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B #0x%02X, R%d%c\n",
               pc - instruction.size, imm8, regIdx, isHigh ? 'H' : 'L');

        return true;
    }

    // BUG61, 2026-09-16 - `0x68` IS `MOV.B @ERd`, AND `LDC @ERn,EXR` DOES NOT EXIST.
    //
    // RENDERED PDF page 801 (printed "page 765 of 988"). Two rows, and they are the only
    // 0x68 entries in Appendix A.1:
    //     MOV.B @ERs,Rd   6 8 | 0:ers rd    2 bytes
    //     MOV.B Rs,@ERd   6 8 | 1:erd rs    2 bytes
    // Flags: N and Z from the byte, V CLEARED, C and H unchanged.
    //
    // THE SAME PAGE DISPROVES THE FICTION IN ITS TOP TWO ROWS: every LDC memory form is
    // `0 1 4x`-prefixed - `LDC @aa:32,CCR = 01 40 6B 20 | abs`, `LDC @aa:32,EXR = 01 41
    // 6B 20 | abs`. A BARE 0x68 IS NEVER AN LDC. That is the identical test that condemned
    // `MOVA.L` (FIX27c), `MOVU.L`/`MOVU.W` (BUG34), the "firmware-specific 0x01 MOV.B"
    // (Defect 7) and `STC VBR,ERn` (BUG52) - the sixth member of that family.
    //
    // AND IT WAS DESTRUCTIVE, NOT MERELY WRONG. The handler loaded a word from wherever
    // ERn happened to point, assigned it to EXR, and then rewrote ALL FIVE CCR flags from
    // its low bits. MEASURED: `[LDC]` fired 551 times in the first 500,000 log lines of
    // the MIDI boot - on a live path, every time corrupting the condition codes the next
    // branch would read. The CCR campaign this project ran on the Virus is the same shape:
    // a correct machine fed a wrong value by us.
    bool H8S2350InstructionExecutor::execute0x68Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        const uint32_t at = (emulator->getProgramCounter() - instruction.size) & 0x00FFFFFFu;
        const uint8_t  b2 = emulator->readByte(at + 1);

        const bool     store = (b2 & 0x80) != 0;
        const uint8_t  ern   = (b2 >> 4) & 0x07;    // 0:ers or 1:erd - THREE bits
        const uint8_t  rfld  =  b2       & 0x0F;    // rd or rs - the FOUR-bit byte field
        const uint32_t address = regs.er[ern] & 0x00FFFFFFu;

        uint8_t value;
        if (store) {
            value = (uint8_t)getRegisterValue(regs, rfld, 0);
            emulator->writeByte(address, value);
        } else {
            value = emulator->readByte(address);
            setRegisterValue(regs, rfld, value, 0);
        }

        flags.zero = (value == 0);
        flags.negative = (value & 0x80) != 0;
        flags.overflow = false;                     // V cleared; C and H UNCHANGED

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B %s @ER%d(=0x%06X) = 0x%02X (R%d%c)\n",
               at, store ? "store ->" : "load <-", ern, address, value,
               rfld & 7, (rfld & 8) ? 'L' : 'H');
        emulator->addCycles(4);
        return true;
    }

    // Handle 0x69 family: Pattern-based decoder per H8S/2350 Software Manual
    // Multiple instructions share 0x69 first byte, differentiated by second byte bit patterns
    bool H8S2350InstructionExecutor::execute0x69Family(const H8S2350Instruction& instruction,
                                                        class H8S2350Emulator* emulator)
    {
        if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-0x69-ENTRY] PC=0x%06X opcode=0x%04X size=%u\n", emulator->getProgramCounter(), instruction.opcode, instruction.size);
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read second byte to determine instruction type via pattern matching
        uint8_t op1 = emulator->readByte(pc - instruction.size + 1);

        // ====================================================================
        // 2026-09-13 - THE 0x69 GROUP HAD NO LOAD PATH AT ALL.
        //
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED pages 765 and 766:
        //     MOV.W @ERs,Rd    6 9   0:ers rd      <- bit 7 CLEAR  = LOAD
        //     MOV.W Rs,@ERd    6 9   1:erd rs      <- bit 7 SET    = STORE
        // and those two rows are the ONLY 0x69 entries in the table.
        //
        // The old code never tested bit 7. Every 0x69 that missed the two "STC"
        // patterns below fell into a store - its own comment said so ("the most
        // common case"). It also read the operands from the wrong halves: the
        // register in bits 6-4 is the ADDRESS register, the one in bits 3-0 is
        // the data register, and the code had them the other way round. There is
        // no H/L bit in a word move; bit 3 is part of the register field.
        //
        // MEASURED, LOOP-0x11CF0: `69 30` at 0x011CF0 is MOV.W @ER3,R0. A probe at
        // 0x011CF0 and one at 0x011CF2 returned BYTE-IDENTICAL register files - the
        // instruction loaded nothing - and instead it wrote R3 into the address in
        // ER0 (0xFFFE), on every one of a million iterations. The BEQ that follows
        // then tested a flag nothing had set, so the loop never exited.
        //
        // This is BUG5's shape exactly, one opcode over: "execute0x6EInstruction
        // ALWAYS stores, never loads ... every 0x6E load in the entire FW run =
        // memory-corrupting store".
        //
        // The two "STC.W" patterns that used to sit here are GONE. Rendered page
        // 806 shows STC.W is a 0x01-PREFIXED four-byte form (`01 40 6B ...` for
        // CCR, `01 41 6B ...` for EXR) - a bare 0x69 is never an STC. Those
        // patterns were swallowing `69 F0`..`69 FF`, which per the table above are
        // MOV.W Rs,@ER7 - stores through the stack pointer.
        // ====================================================================
        {
            const bool    isStore = (op1 & 0x80) != 0;
            const uint8_t ern     = (op1 >> 4) & 0x07;   // address register (ERs on load, ERd on store)
            const uint8_t rn      =  op1       & 0x07;   // data register    (Rd   on load, Rs   on store)
            const bool    rnIsE   = (op1 & 0x08) != 0;   // BUG112: 16-bit field 8-15 = E0-E7 (HM A legend, rendered p.807) - was masked to R0-R7.
            const uint32_t address = regs.er[ern] & 0x00FFFFFF;

            uint16_t value;
            if (isStore) {
                value = rnIsE ? regs.e[rn] : uint16_t(regs.r[rn] & 0xFFFF);
                emulator->storeWord(address, value);   // BUG126: one bus cycle into CS0
                emulator->addCycles(H8S_CYC_BASE_MEM);
                emulator->addCycles(emulator->memWritePenalty(address));
            } else {
                value = uint16_t((uint16_t(emulator->readByte(address)) << 8) |
                                  uint16_t(emulator->readByte(address + 1)));
                if (rnIsE) { regs.e[rn] = value; emulator->syncRegAfterUpperWordWrite(rn); }
                else       { regs.r[rn] = value; emulator->syncRegAfterWordWrite(rn); }
                emulator->addCycles(H8S_CYC_BASE_MEM);
                emulator->addCycles(emulator->memReadPenalty(address));
            }

            // MOV sets N and Z from the value moved and clears V; C is untouched.
            flags.zero     = (value == 0);
            flags.negative = (value & 0x8000) != 0;
            flags.overflow = false;

            if (!g_h8s_quiet_boot) {
                if (isStore) printf("[DECODE] 0x%06X: MOV.W R%d(=0x%04X), @ER%d (addr=0x%06X)\n",
                                    pc - instruction.size, rn, value, ern, address);
                else         printf("[DECODE] 0x%06X: MOV.W @ER%d(addr=0x%06X), R%d = 0x%04X\n",
                                    pc - instruction.size, ern, address, rn, value);
            }
            return true;
        }

    }

    // Handle 0x6C instruction (MOV.B @ERn, Rd)
    bool H8S2350InstructionExecutor::execute0x6CInstruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // BUG60, 2026-09-16 - `0x6C` is BUG3's shape one opcode over, FOUR ways at once.
        // Renesas HM Rev 3.00, Appendix A.1, and the two rows are the ONLY 0x6C entries:
        //
        //     MOV.B @ERs+,Rd   6 C | 0:ers rd    2 bytes   @ERs -> Rd8, ERs32+1 -> ERs32
        //     MOV.B Rs,@-ERd   6 C | 1:erd rs    2 bytes   ERd32-1 -> ERd32, Rs8 -> @ERd
        //
        // Flags for both: I- H- N* Z* V=0 C-.
        //
        // What stood here was wrong in four independent ways:
        //  (1) BIT 7 WAS NEVER TESTED, so there was NO STORE PATH AT ALL - exactly BUG3's
        //      defect on 0x69, and exactly BUG34's on the prefixed 0x6D.
        //  (2) THE NIBBLES WERE SWAPPED. Its own comment says "destination register in
        //      high nibble, ER register in low nibble". The manual says the HIGH nibble is
        //      `0:ers` - the ADDRESS register - and the LOW nibble is `rd`, the DATA one.
        //  (3) The register field was masked to four bits and then REJECTED at >= 8 - one
        //      of the 23 live `>= 8` guards BUG33 counted. 8-15 are R0L..R7L.
        //  (4) It never touched ERs at all, so the POST-INCREMENT that is half the
        //      instruction's meaning simply did not happen.
        //
        // MEASURED: `[ERROR] Invalid register indices in 0x6C instruction: rd=1 ern=8 at
        // PC 0x407D22` halted the boot immediately after the firmware had driven the
        // HD44780 through Entry Mode Set (0x06) and Display ON (0x0C). `ern=8` is bit 7
        // set - the store form - so the guard was rejecting the row that does not exist
        // in this handler.
        const uint32_t at = (pc - instruction.size) & 0x00FFFFFFu;
        const uint8_t  b2 = emulator->readByte(at + 1);
        const bool     store = (b2 & 0x80) != 0;
        const uint8_t  ern   = (b2 >> 4) & 0x07;   // 0:ers or 1:erd - THREE bits
        const uint8_t  rfld  =  b2       & 0x0F;   // rd or rs - the FOUR-bit byte field

        uint8_t value;
        uint32_t address;
        if (store) {                                // MOV.B Rs,@-ERd : pre-DECREMENT
            address = (regs.er[ern] - 1) & 0x00FFFFFFu;
            regs.er[ern] = (regs.er[ern] & 0xFF000000u) | address;
            emulator->syncRegAfterLongWrite(ern);
            value = (uint8_t)getRegisterValue(regs, rfld, 0);
            emulator->writeByte(address, value);
        } else {                                    // MOV.B @ERs+,Rd : post-INCREMENT
            address = regs.er[ern] & 0x00FFFFFFu;
            value = emulator->readByte(address);
            setRegisterValue(regs, rfld, value, 0);
            const uint32_t next = (address + 1) & 0x00FFFFFFu;
            regs.er[ern] = (regs.er[ern] & 0xFF000000u) | next;
            emulator->syncRegAfterLongWrite(ern);
        }

        flags.zero = (value == 0);
        flags.negative = (value & 0x80) != 0;
        flags.overflow = false;                     // V is CLEARED; C and H are UNCHANGED

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s ER%d @0x%06X = 0x%02X (R%d%c)\n",
               at, store ? "MOV.B Rs,@-" : "MOV.B @ERs+,", ern, address, value,
               rfld & 7, (rfld & 8) ? 'L' : 'H');
        emulator->addCycles(4);
        return true;
    }

    // Handle 0x6E instruction: MOV.B @(d:16,ERs),Rd (load) OR MOV.B Rs,@(d:16,ERd) (store)
    // H8S/2600 Manual: operand byte = b7 | ers/erd:3 | rd/rs:4
    //   b7=0: LOAD  MOV.B @(d:16,ERs),Rd  -> ers=(op>>4)&7, rd=op&0xF
    //   b7=1: STORE MOV.B Rs,@(d:16,ERd)  -> rs=op&0xF, erd=(op>>4)&7
    // Rd/Rs = 0-7 = RnH, 8-15 = RnL
    bool H8S2350InstructionExecutor::execute0x6EInstruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t start_pc = pc - instruction.size;

        // Read operand byte (byte 1 of instruction)
        uint8_t operand = emulator->readByte(start_pc + 1);
        
        // Read 16-bit displacement (big-endian, bytes 2-3)
        uint16_t displacement = ((uint16_t)emulator->readByte(start_pc + 2) << 8) |
                                ((uint16_t)emulator->readByte(start_pc + 3));
        displacement = (int16_t)displacement;  // Sign-extend

        bool is_load = (operand & 0x80) == 0;  // b7=0 -> load, b7=1 -> store

        if (is_load) {
            // LOAD: MOV.B @(d:16,ERs),Rd
            uint8_t ers = (operand >> 4) & 0x07;    // bits 6-4: ERs index (0-7)
            uint8_t rd  = operand & 0x0F;           // bits 3-0: destination reg (0-15)
            
            // Validate ER index
            if (ers >= 8) {
                fprintf(stderr, "[ERROR] 0x6E LOAD invalid ERs=%d at PC=0x%06X\n", ers, start_pc);
                emulator->halt();
                return false;
            }

            // Compute effective address: ER[ers] + displacement (signed 16-bit)
            int32_t disp_signed = (int16_t)displacement;
            uint32_t address = regs.er[ers] + disp_signed;
            
            // Read byte from memory
            uint8_t value = emulator->readByte(address & 0x00FFFFFF);
            
            // Write to destination register (0-7=RnH, 8-15=RnL)
            if (rd <= 7) {
                regs.rh[rd] = value;
            } else {
                regs.rl[rd - 8] = value;
            }
            // BUG107: this passed `rd` (8..15 for RnL) to a function that ignores any index >= 8,
            // so a load into R0L..R7L reached rl[] and NEVER r[]/er[] - the next word, long or
            // address use of that register saw the old value. MEASURED at 0x00A01A, `6E 28 00 00`
            // = MOV.B @(0,ER2),R0L with @0x3014C = 0xF0: flags came from 0xF0 (BMI/BCC taken)
            // but ER0 stayed 0, so SUB.W #0xF0,R0 gave 0xFF10, the jump-table index became
            // 0x3FC40, and JMP @ER0 at 0x00A074 went to 0xF31D44 - the PC-OFFMAP that has
            // stopped every boot at ~4.8 s since at least 2026-09-19.
            emulator->syncRegAfterByteWrite(rd & 7, rd <= 7);

            // Table A.1 (1), RENDERED page 771 (printed 735): MOV.B @(d:16,ERs),Rd -
            // N and Z change, V = 0, C UNCHANGED. The old `carry = false` was a fabrication.
            flags.zero = (value == 0);
            flags.negative = (value & 0x80) != 0;
            flags.overflow = false;

            if (!g_h8s_quiet_boot) fprintf(stderr, "[DECODE] 0x%06X: MOV.B @(0x%04X,ER%d)->R%c%d = 0x%02X\n",
                   start_pc, displacement, ers, (rd <= 7 ? 'H' : 'L'), rd & 7, value);
        } else {
            // STORE: MOV.B Rs,@(d:16,ERd)
            uint8_t rs  = operand & 0x0F;           // bits 3-0: source reg (0-15)
            uint8_t erd = (operand >> 4) & 0x07;    // bits 6-4: ERd index (0-7)

            if (erd >= 8) {
                fprintf(stderr, "[ERROR] 0x6E STORE invalid ERd=%d at PC=0x%06X\n", erd, start_pc);
                emulator->halt();
                return false;
            }

            // Get source register value (0-7=RnH, 8-15=RnL)
            uint8_t value;
            if (rs <= 7) {
                value = regs.rh[rs];
            } else {
                value = regs.rl[rs - 8];
            }

            // Compute effective address
            int32_t disp_signed = (int16_t)displacement;
            uint32_t address = regs.er[erd] + disp_signed;

            // Write byte to memory
            emulator->writeByte(address & 0x00FFFFFF, value);

            // Table A.1 (1), RENDERED page 771: MOV.B Rs,@(d:16,ERd) - N Z change, V = 0,
            // C UNCHANGED (BUG107: `carry = false` removed).
            flags.zero = (value == 0);
            flags.negative = (value & 0x80) != 0;
            flags.overflow = false;

            if (!g_h8s_quiet_boot) fprintf(stderr, "[DECODE] 0x%06X: MOV.B R%c%d=0x%02X -> @(0x%04X,ER%d)\n",
                   start_pc, (rs <= 7 ? 'H' : 'L'), rs & 7, value, displacement, erd);
        }

        return true;
    }

    // 0x78 = MOV.B/MOV.W @(d:32, ERn) <-> Rd  (Renesas H8S/2600 p.284/285). 10-byte instruction.
    bool H8S2350InstructionExecutor::execute0x78Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t ern = (uint8_t)instruction.source_operand & 0x07;          // base ER register
        uint8_t b3  = (uint8_t)((instruction.immediate_value >> 8) & 0xFF); // 0x6A or 0x6B
        uint8_t b4  = (uint8_t)(instruction.immediate_value & 0xFF);        // dir|reg
        // 32-bit displacement: bytes 5..8 (base+6 .. base+9)
        uint32_t disp = ((uint32_t)emulator->readByte(base+4) << 24)
                      | ((uint32_t)emulator->readByte(base+5) << 16)
                      | ((uint32_t)emulator->readByte(base+6) << 8)
                      |  (uint32_t)emulator->readByte(base+7);
        bool isWord  = (b3 == 0x6B);
        bool isStore = ((b4 & 0xF0) == 0xA0);
        uint8_t reg  = b4 & 0x0F;                 // data register (0-7=RnH, 8-15=RnL for byte)
        uint32_t addr = (regs.er[ern] + disp) & 0x00FFFFFF;

        if (isStore) {
            if (isWord) {
                // BUG95: 16-BIT REGISTER FIELD, RENDERED page 807 (printed 771) legend:
                //   0000-0111 = R0..R7, 1000-1111 = E0..E7. `& 0x07` collapsed E0 onto R0.
                //   The form itself: RENDERED page 772 (printed 736), MOV.W Rs,@(d:32,ERd),
                //   size W, length 8, Rs16 -> @(d:32,ERd), N and Z change, V = 0, C untouched.
                //   The BYTE paths in this same function already honoured bit 3.
                const uint8_t idx  = reg & 0x07;
                const bool    isRn = (reg & 0x08) == 0;
                uint16_t val = isRn ? regs.r[idx] : regs.e[idx];
                emulator->writeWord(addr, val);
                // BUG122 (DIFFREF MOV oracle): the store forms set no flag at all. REJ09B0139 MOV:
                // N and Z from the data, V = 0, C unchanged - for stores exactly as for loads.
                flags.zero = (val == 0); flags.negative = (val & 0x8000) != 0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W %s%d,@(0x%08X,ER%d)=@0x%06X val=0x%04X\n",
                       base, isRn ? "R" : "E", idx, disp, ern, addr, val);
            } else {
                uint8_t regIdx = reg & 0x07;
                bool isHigh = (reg < 8);
                uint8_t val = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
                emulator->writeByte(addr, val);
                flags.zero = (val == 0); flags.negative = (val & 0x80) != 0; flags.overflow = false;   // BUG122
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B R%d%c,@(0x%08X,ER%d)=@0x%06X val=0x%02X\n",
                       base, regIdx, isHigh ? 'H' : 'L', disp, ern, addr, val);
            }
        } else {
            if (isWord) {
                // BUG95: same 16-bit register field as the store above.
                //   RENDERED page 772 (printed 736): MOV.W @(d:32,ERs),Rd, size W, length 8,
                //   @(d:32,ERs) -> Rd16, N and Z change, V = 0, C untouched.
                const uint8_t idx  = reg & 0x07;
                const bool    isRn = (reg & 0x08) == 0;
                uint16_t val = emulator->readWord(addr);
                if (isRn) { regs.r[idx] = val; emulator->syncRegAfterWordWrite(idx); }
                else      { regs.e[idx] = val; emulator->syncRegAfterUpperWordWrite(idx); }
                flags.zero = (val == 0);
                flags.negative = (val & 0x8000) != 0;
                flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W @(0x%08X,ER%d)=@0x%06X,%s%d val=0x%04X\n",
                       base, disp, ern, addr, isRn ? "R" : "E", idx, val);
            } else {
                uint8_t regIdx = reg & 0x07;
                bool isHigh = (reg < 8);
                uint8_t val = emulator->readByte(addr);
                if (isHigh) regs.rh[regIdx] = val; else regs.rl[regIdx] = val;
                emulator->syncRegAfterByteWrite(regIdx, isHigh);
                flags.zero = (val == 0);
                flags.negative = (val & 0x80) != 0;
                flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B @(0x%08X,ER%d)=@0x%06X,R%d%c val=0x%02X\n",
                       base, disp, ern, addr, regIdx, isHigh ? 'H' : 'L', val);
            }
            }
            if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-0x69] execute0x69Family done, PC=0x%06X\n", emulator->getProgramCounter());
            return true;
    }

    // Handle 0x79 instruction (MOV.W @(disp16, ERn), Rd)
    bool H8S2350InstructionExecutor::execute0x79Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;   // address of the 0x79 byte
        uint8_t sub = (uint8_t)instruction.source_operand;       // ALU sub-op (0..6)
        // BUG35, 2026-09-13 - THE 16-BIT REGISTER FIELD'S UPPER HALF WAS THROWN AWAY.
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.2 legend, RENDERED page 771:
        //     16-Bit Register:  0000-0111 -> R0..R7      1000-1111 -> E0..E7
        // `& 0x07` collapses E2 onto R2, so a firmware routine that keeps a counter in
        // E2 and its data in R2 - both halves of ER2, which is normal H8S practice -
        // has its counter destroyed by its own data load every iteration.
        // Measured at 0x01CE14, `79 0A 00 6C` = MOV.W #0x006C,E2, with MS2K_LOOPWATCH:
        //     ER2 = 000A006C   the 0x6C landed in R2, E2 untouched
        // and the copy loop at 0x01CE18 then ran 497+ times instead of 108.
        // getRegisterValue/setRegisterValue with size==1 already implement the legend -
        // this handler simply did not use them. Same skeleton as BUG29 (byte halves).
        uint8_t rd  = (uint8_t)instruction.destination_operand & 0x0F;
        uint16_t imm16 = (uint16_t)((emulator->readByte(base+2) << 8) | emulator->readByte(base+3));
        uint16_t a = (uint16_t)getRegisterValue(regs, rd, 1);
        uint32_t res32 = a;
        const char* nm = "?";
        bool writeBack = true, isCmp = false;
        switch (sub) {
            case 0x0: res32 = imm16;            nm="MOV.W"; break;
            case 0x1: res32 = (uint32_t)a + imm16; nm="ADD.W"; break;
            case 0x2: res32 = (uint32_t)a - imm16; nm="CMP.W"; writeBack=false; isCmp=true; break;
            case 0x3: res32 = (uint32_t)a - imm16; nm="SUB.W"; break;
            case 0x4: res32 = a | imm16;        nm="OR.W";  break;
            case 0x5: res32 = a ^ imm16;        nm="XOR.W"; break;
            case 0x6: res32 = a & imm16;        nm="AND.W"; break;
            default:
                printf("[ERROR] 0x79 unknown sub-op 0x%X at PC 0x%06X\n", sub, base);
                emulator->handleIllegalInstruction();
                return true;
        }
        uint16_t res = (uint16_t)(res32 & 0xFFFF);
        // Flags: N,Z always; V/C for arithmetic; logical clears V (C untouched).
        flags.zero = (res == 0);
        flags.negative = (res & 0x8000) != 0;
        if (sub==0x1 || sub==0x2 || sub==0x3) {
            // arithmetic: set C (carry/borrow) and V (overflow)
            if (sub==0x1) { flags.carry = (res32 > 0xFFFF); }
            else { flags.carry = (a < imm16); } // borrow for SUB/CMP
            // BUG121: H = carry/borrow at bit 11 (REJ09B0139 RENDERED p.49 ADD.W, p.92 CMP.W,
            // p.235 SUB.W); it was never written by the #xx:16 forms.
            flags.half_carry = ((uint32_t(a) ^ uint32_t(imm16) ^ res32) & 0x1000u) != 0;
            bool sa=(a&0x8000)!=0, sb=(imm16&0x8000)!=0, sr=(res&0x8000)!=0;
            flags.overflow = (sub==0x1) ? ((sa==sb) && (sr!=sa)) : ((sa!=sb) && (sr!=sa));
        } else if (sub==0x0) {
            flags.overflow = false;
        } else { // OR/XOR/AND
            flags.overflow = false;
        }
        if (writeBack) {
            setRegisterValue(regs, rd, res, 1);
            if ((rd & 0x07) == 7) emulator->syncRegAfterLongWrite(7);   // keep the SP mirror honest
        }
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s #0x%04X, %s%d%s = 0x%04X\n",
               base, nm, imm16, (rd & 0x08) ? "E" : "R", rd & 0x07,
               isCmp ? " (compare)" : "", res);
        return true;
    }

    // Handle 0x07 instruction (MOV.W @(d:16, Rn), Rd) - boot2.txt: Unified PC advancement
    bool H8S2350InstructionExecutor::execute0x07Instruction(const H8S2350Instruction& instruction,
                                                             class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte directly from memory (avoid fetch8 double advancement)
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement directly from memory (avoid fetch16 double advancement)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                                ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // boot3.txt: Extract register indices using 3-bit macros (H8S standard)
        // Format: 0x07 ern:3 rd:3 (bits 4-6 = ER, bits 0-2 = Rd, bit 7/bit3 = mode flags)
        uint8_t ern = R_HI3(operand);  // ER register index (0-7) - 3 bits
        uint8_t rd = R_LO3(operand);   // Destination register (0-7) - 3 bits

        // boot3.txt: Debug operand extraction
        if (!g_h8s_quiet_boot) printf("[DEBUG] 0x07 operand=0x%02X, ern=%d, rd=%d, mode_hi=%d, mode_lo=%d\n",
               operand, ern, rd, MODE_HI(operand), MODE_LO(operand));

        // Validate register indices
        if (rd >= 8 || ern >= 8) {
            printf("[ERROR] Invalid register indices in 0x07 instruction: rd=%d ern=%d at PC 0x%06X\n", rd, ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true; // Return true to continue execution from exception handler
        }

        // Calculate effective address: ERn + displacement
        uint32_t address = regs.er[ern] + displacement;

        // Read word from memory (big-endian)
        uint16_t value = ((uint16_t)emulator->readByte(address) << 8) |
                          ((uint16_t)emulator->readByte(address + 1));

        // Write to destination register (update only lower 16 bits)
        regs.r[rd] = (regs.r[rd] & 0xFFFF0000) | value;
        emulator->syncRegAfterWordWrite(rd);

        // Update flags based on the moved value
        flags.zero = (value == 0);
        flags.negative = (value & 0x8000) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.W @(0x%04X, ER%d)(=0x%06X)(=0x%04X), R%d\n", pc - instruction.size, displacement, ern, address, value, rd);

        return true;
    }

    // Handle 0x08 instruction (MOV.W Rs, Rd)
    bool H8S2350InstructionExecutor::execute0x08Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the second byte to get source and destination register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register indices from the operand byte
        // Format: 0x08 rs:4 rd:4 (source register in high nibble, destination register in low nibble)
        uint8_t rs = (operand >> 4) & 0x0F;  // Source register (0-7)
        uint8_t rd = operand & 0x0F;         // Destination register (0-7)

        // Validate register indices
        if (rs >= 8 || rd >= 8) {
            printf("[ERROR] Invalid register indices in 0x08 instruction: rs=%d rd=%d at PC 0x%06X\n", rs, rd, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true; // Return true to continue execution from exception handler
        }

        // Move word from source register to destination register
        uint16_t value = regs.r[rs] & 0xFFFF;
        regs.r[rd] = (regs.r[rd] & 0xFFFF0000) | value;  // Update only lower 16 bits
        emulator->syncRegAfterWordWrite(rd);

        // Update flags based on the moved value
        flags.zero = (value == 0);
        flags.negative = (value & 0x8000) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.W R%d, R%d (value=0x%04X)\n", pc - instruction.size, rs, rd, value);

        return true;
    }

    // BUG93: ADD.B Rs,Rd = 08 rs rd. IT WAS DECODED AND NEVER EXECUTED.
    //
    // The decoder produced a complete instruction for 0x08 with the mnemonic
    // "ADD.B Rs,Rd" and a size of 2, and then the executor's dispatch had NO
    // case 0x08 anywhere - so the PC advanced two bytes and the addition simply
    // did not happen. Nothing reported it: [OPCODE-MISSING] stayed at zero
    // because the opcode WAS decoded. A silent two-byte NOP.
    //
    // RENDERED page 774 (printed 738), Appendix A (2) Arithmetic Instructions:
    //   ADD.B Rs,Rd   operand size B, addressing Rn, length 2,
    //   operation "Rd8 + Rs8 -> Rd8", condition code I - , H, N, Z, V, C all
    //   marked changed, 1 state.
    // The rs/rd fields are FOUR bits each: 0-7 = RnH, 8-15 = RnL (RENDERED page
    // 771), which is why a masked `& 0x07` version of this would be wrong the
    // way BUG88 and BUG89 were wrong.
    //
    // MEASURED against the Tier-2 reference at 0x00472E: with R3 = 0x0100 the
    // firmware runs MOV.B R3L,R0L then ADD.B R3H,R0L and needs R0L = 0x01. Ours
    // stayed 0x00, so ER1 never left 0, and the table walk at 0x00486C wrote
    // 0x402722 twenty-four times instead of stepping 0x402723, 25, 27, 29 ...
    bool H8S2350InstructionExecutor::executeADD_B_REG_REG(const H8S2350Instruction& instruction,
                                                          class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);
        const uint8_t rs = (operand >> 4) & 0x0F;
        const uint8_t rd = operand & 0x0F;
        const uint8_t src = (rs < 8) ? regs.rh[rs & 7] : regs.rl[rs & 7];
        const uint8_t dst = (rd < 8) ? regs.rh[rd & 7] : regs.rl[rd & 7];
        const uint16_t sum = uint16_t(dst) + uint16_t(src);
        const uint8_t  res = uint8_t(sum & 0xFFu);
        if (rd < 8) regs.rh[rd & 7] = res; else regs.rl[rd & 7] = res;
        emulator->syncRegAfterByteWrite(rd & 7, (rd < 8));
        flags.carry      = (sum > 0xFFu);
        flags.half_carry = (((dst & 0x0Fu) + (src & 0x0Fu)) > 0x0Fu);
        flags.zero       = (res == 0);
        flags.negative   = (res & 0x80u) != 0;
        flags.overflow   = ((~(uint8_t)(dst ^ src) & (uint8_t)(dst ^ res)) & 0x80u) != 0;
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADD.B R%d%c -> R%d%c (0x%02X + 0x%02X = 0x%02X)\n",
               pc - instruction.size, rs & 7, (rs<8)?'H':'L', rd & 7, (rd<8)?'H':'L', dst, src, res);
        return true;
    }

    // BUG105: ADD.W Rs,Rd = 09 rs rd (Table A.2, RENDERED page 794, printed 758).
    // Table A.1 (2), RENDERED page 774 (printed 738): size W, Rn, length 2,
    // "Rd16+Rs16 -> Rd16", I -, H [3], N Z V C changed, 1 state. Note [3] (RENDERED page 792,
    // printed 756): "Set to 1 when a carry or borrow occurs at bit 11". rs/rd are FOUR-bit fields, 0-7 = R0..R7, 8-15 = E0..E7
    // (the same legend BUG96 read for MOV.W Rs,Rd).
    bool H8S2350InstructionExecutor::executeADD_W_REG_REG(const H8S2350Instruction& instruction,
                                                          class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        const uint8_t operand = emulator->readByte(pc - instruction.size + 1);
        const uint8_t rs = (operand >> 4) & 0x0F, rd = operand & 0x0F;
        const uint8_t si = rs & 0x07, di = rd & 0x07;
        const uint16_t src = (rs & 0x08) ? regs.e[si] : regs.r[si];
        const uint16_t dst = (rd & 0x08) ? regs.e[di] : regs.r[di];
        const uint32_t sum = uint32_t(dst) + uint32_t(src);
        const uint16_t res = uint16_t(sum & 0xFFFFu);
        if (rd & 0x08) { regs.e[di] = res; emulator->syncRegAfterUpperWordWrite(di); }
        else           { regs.r[di] = res; emulator->syncRegAfterWordWrite(di); }
        flags.carry      = sum > 0xFFFFu;
        flags.half_carry = ((dst & 0x0FFFu) + (src & 0x0FFFu)) > 0x0FFFu;
        flags.zero       = res == 0;
        flags.negative   = (res & 0x8000u) != 0;
        flags.overflow   = ((~(dst ^ src) & (dst ^ res)) & 0x8000u) != 0;
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADD.W %c%d -> %c%d (0x%04X + 0x%04X = 0x%04X)\n",
               pc - instruction.size, (rs & 8) ? 'E' : 'R', si, (rd & 8) ? 'E' : 'R', di, dst, src, res);
        return true;
    }

    // MOV.B Rs, Rd = 0C rs rd (Renesas p.284). rs/rd: 0-7=RnH, 8-15=RnL.
    bool H8S2350InstructionExecutor::executeMOV_B_REG_REG(const H8S2350Instruction& instruction,
                                                          class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);
        uint8_t rs = (operand >> 4) & 0x0F;
        uint8_t rd = operand & 0x0F;
        uint8_t val = (rs < 8) ? regs.rh[rs & 7] : regs.rl[rs & 7];
        if (rd < 8) regs.rh[rd & 7] = val; else regs.rl[rd & 7] = val;
        emulator->syncRegAfterByteWrite(rd & 7, (rd < 8));
        flags.zero = (val == 0);
        flags.negative = (val & 0x80) != 0;
        flags.overflow = false;
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B R%d%c -> R%d%c = 0x%02X\n", pc - instruction.size,
               rs & 7, (rs<8)?'H':'L', rd & 7, (rd<8)?'H':'L', val);
        return true;
    }

    // MOV.W Rs, Rd = 0D rs rd (Renesas p.285). rs/rd: 0-7 = R0-R7 (16-bit).
    bool H8S2350InstructionExecutor::executeMOV_W_REG_REG(const H8S2350Instruction& instruction,
                                                          class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        // BUG96, 2026-09-19 - `MOV.W Rs,Rd` MASKED ITS REGISTER FIELD TO THREE BITS.
        // RENDERED page 771 (printed 735), Table A.1 (1) Data Transfer Instructions:
        //   MOV.W Rs,Rd   size W, addressing Rn, length 2, Rs16 -> Rd16,
        //   N and Z change, V = 0, C untouched, 1 state.
        // The field is FOUR bits, Appendix A.2 legend RENDERED page 807 (printed 771):
        //   0000-0111 = R0..R7, 1000-1111 = E0..E7.
        // This is BUG35 / BUG73 / BUG83 / BUG95's defect a sixth time, and again on
        // the live boot path. MEASURED at 0x004EB2, `0D 88` = MOV.W E0,E0 - the
        // panel/ADC deadband filter's own zero test - which ran as MOV.W R0,R0 with
        // R0 = 0, so Z came up 1 and the BEQ at 0x004EB4 jumped to the RTS at 0x004ED6
        // every time, skipping the table write at 0x004EBC.
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);
        const uint8_t rs = (operand >> 4) & 0x0F;
        const uint8_t rd = operand & 0x0F;
        const uint8_t si = rs & 0x07, di = rd & 0x07;
        const bool sIsRn = (rs & 0x08) == 0, dIsRn = (rd & 0x08) == 0;
        uint16_t val = sIsRn ? regs.r[si] : regs.e[si];
        if (dIsRn) { regs.r[di] = val; emulator->syncRegAfterWordWrite(di); }
        else       { regs.e[di] = val; emulator->syncRegAfterUpperWordWrite(di); }
        flags.zero = (val == 0);
        flags.negative = (val & 0x8000) != 0;
        flags.overflow = false;
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W %s%d -> %s%d = 0x%04X\n",
               pc - instruction.size, sIsRn ? "R" : "E", si, dIsRn ? "R" : "E", di, val);
        return true;
    }

    // Handle 0x6A instruction (0x6A unary register group: EXTU/EXTS/INC/DEC/etc.)
    bool H8S2350InstructionExecutor::execute0x6AInstruction(const H8S2350Instruction& instruction,
                                                             class H8S2350Emulator* emulator)
    {
        if (!g_h8s_quiet_boot) fprintf(stderr, "[DEBUG-0x6A-ENTRY] operation=0x%X dest=0x%X\n", instruction.source_operand, instruction.destination_operand);
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Get pre-decoded operation and register information from instruction
        uint8_t operation = instruction.source_operand;  // Operation code (from high nibble)
        uint8_t reg_idx = instruction.destination_operand; // Register index (from low nibble)

        // reg_idx is the low nibble (0-15); the actual register is (reg_idx & 7).
        // For store ops (8/A) the nibble high bit distinguishes the encoding, handled below.

        // Base address of this instruction (PC was already advanced by instruction.size)
        uint32_t base = pc - instruction.size;

        switch (operation) {
            // =================================================================
            // BUG36, 2026-09-13. Two defects across these four rows, and the
            // second one is why the firmware's TPU2 was never configured.
            //
            // Encodings, Renesas H8S/2350 HM Rev 3.00, Appendix A.1,
            // RENDERED PDF page 801 (printed "page 765 of 988"):
            //
            //   MOV.B @aa:16,Rd   6 A 0 rd | abs16       4 bytes
            //   MOV.B @aa:32,Rd   6 A 2 rd | abs32       6 bytes
            //   MOV.B Rs,@aa:16   6 A 8 rs | abs16       4 bytes
            //   MOV.B Rs,@aa:32   6 A A rs | abs32       6 bytes
            //
            // (a) `case 0xA` - MOV.B Rs,@aa:32 - HAD NO STORE AT ALL. It printed a
            //     debug line that --quiet-boot suppresses and fell out of the
            //     switch. The PC advanced, so nothing looked wrong, and every
            //     byte the firmware writes to a peripheral through a 32-bit
            //     absolute address was DISCARDED. Measured at 0x0042E8 onward:
            //
            //       0042E8: 6A A9 00 FF FF F0   MOV.B R1L,@0xFFFFF0   TCR2  = 0x29
            //       0042F0: 6A A9 00 FF FF F1   MOV.B R1L,@0xFFFFF1   TMDR2 = 0xC0
            //       0042F8: 6A A9 00 FF FF F4   MOV.B R1L,@0xFFFFF4   TIER2 = 0x41
            //       004300: 6A A9 00 FF FF F5   MOV.B R1L,@0xFFFFF5   TSR2  = 0xC0
            //
            //     None of the four reached tpgWrite. TIER2 = 0x41 is TGIEA SET -
            //     the firmware DOES enable the compare-match interrupt - and TCR2
            //     = 0x29 is its prescaler and counter-clear setting. Without them
            //     the TPU model sat at its reset values forever and could never
            //     raise TGI2A, which is why a scaffold had to fake the interrupt.
            //     Same shape as BUG34: a silent hole with correct instruction size.
            //
            // (b) all four rows took the register as `regs.rl[reg_idx & 7]`, i.e.
            //     ALWAYS the low half. The 8-bit register field is FOUR bits,
            //     Appendix A.2 legend RENDERED page 771: 0000-0111 = R0H..R7H,
            //     1000-1111 = R0L..R7L. So every RnH form used RnL. This is live
            //     on the current frontier path - 0x012DE2 is
            //     `6A 30 00 40 1A 90` = MOV.B @0x00401A90,R0H followed by
            //     `73 00` = BTST #0,R0H, and the load was landing in R0L while
            //     the test read R0H. BUG29's family again.
            // =================================================================
            case 0x0: { // MOV.B @aa:16, Rd
                uint16_t addr16 = (uint16_t)((emulator->readByte(base + 2) << 8) | emulator->readByte(base + 3));
                uint32_t addr = (addr16 & 0x8000) ? (0xFFFF0000u | addr16) : addr16; // sign-extend to 24/32
                uint8_t val = emulator->readByte(addr & 0x00FFFFFF);
                setRegisterValue(regs, reg_idx, val, 0);
                flags.zero = (val == 0); flags.negative = (val & 0x80) != 0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B @0x%04X, R%d%c = 0x%02X\n",
                       base, addr16, reg_idx & 7, (reg_idx & 8) ? 'L' : 'H', val);
                break;
            }
            case 0x2: { // MOV.B @aa:32, Rd
                uint32_t addr = ((uint32_t)emulator->readByte(base + 2) << 24) | ((uint32_t)emulator->readByte(base + 3) << 16)
                              | ((uint32_t)emulator->readByte(base + 4) << 8)  | emulator->readByte(base + 5);
                uint8_t val = emulator->readByte(addr & 0x00FFFFFF);
                setRegisterValue(regs, reg_idx, val, 0);
                flags.zero = (val == 0); flags.negative = (val & 0x80) != 0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B @0x%08X, R%d%c = 0x%02X\n",
                       base, addr, reg_idx & 7, (reg_idx & 8) ? 'L' : 'H', val);
                break;
            }
            case 0x8: { // MOV.B Rs, @aa:16
                uint16_t addr16 = (uint16_t)((emulator->readByte(base + 2) << 8) | emulator->readByte(base + 3));
                uint32_t addr = (addr16 & 0x8000) ? (0xFFFF0000u | addr16) : addr16;
                uint8_t val = (uint8_t)getRegisterValue(regs, reg_idx, 0);
                emulator->writeByte(addr & 0x00FFFFFF, val);
                flags.zero = (val == 0); flags.negative = (val & 0x80) != 0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B R%d%c=0x%02X -> @0x%04X\n",
                       base, reg_idx & 7, (reg_idx & 8) ? 'L' : 'H', val, addr16);
                break;
            }
            case 0xA: { // MOV.B Rs, @aa:32
                uint32_t addr = ((uint32_t)emulator->readByte(base + 2) << 24) | ((uint32_t)emulator->readByte(base + 3) << 16)
                              | ((uint32_t)emulator->readByte(base + 4) << 8)  | emulator->readByte(base + 5);
                uint8_t val = (uint8_t)getRegisterValue(regs, reg_idx, 0);
                emulator->writeByte(addr & 0x00FFFFFF, val);
                flags.zero = (val == 0); flags.negative = (val & 0x80) != 0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B R%d%c=0x%02X -> @0x%08X\n",
                       base, reg_idx & 7, (reg_idx & 8) ? 'L' : 'H', val, addr);
                break;
            }
            case 0x1:   // BIT-manip @aa:16  (6A 18 <abs16> <bitop> <imm>)  6 bytes
            case 0x3: { // BIT-manip @aa:32  (6A 38 <abs32> <bitop> <imm>)  8 bytes
                // Renesas H8S/2600 manual p.282. Layout after the 0x6A sub-byte:
                //   op=0x1: bytes[2..3] = abs16, bytes[4]=bitop, bytes[5]=imm
                //   op=0x3: bytes[2..5] = abs32, bytes[6]=bitop, bytes[7]=imm
                uint32_t addr; uint8_t bitop, immByte;
                if (operation == 0x1) {
                    uint16_t a16 = (uint16_t)((emulator->readByte(base+2)<<8) | emulator->readByte(base+3));
                    addr = (a16 & 0x8000) ? (0xFFFF0000u | a16) : a16;
                    bitop = emulator->readByte(base+4);
                    immByte = emulator->readByte(base+5);
                } else {
                    addr = ((uint32_t)emulator->readByte(base+2)<<24)|((uint32_t)emulator->readByte(base+3)<<16)
                         | ((uint32_t)emulator->readByte(base+4)<<8) | emulator->readByte(base+5);
                    bitop = emulator->readByte(base+6);
                    immByte = emulator->readByte(base+7);
                }
                addr &= 0x00FFFFFF;
                // BUG120, 2026-09-25 - THE @aa BIT GROUP HAD ONLY HALF ITS ROWS.
                // HM Rev 3.00 Appendix A.2, RENDERED pp.758-762/799: after the address come
                // the operation byte and `i:IMM:0` (immediate bit number, bit 7 = the
                // inverting form) or `rn:0` (bit number in a register, 0x6x rows):
                //   60 BSET Rn  61 BNOT Rn  62 BCLR Rn  63 BTST Rn    67 BST/BIST #xx:3
                //   70 BSET  71 BNOT  72 BCLR  73 BTST  74 BOR/BIOR  75 BXOR/BIXOR
                //   76 BAND/BIAND  77 BLD/BILD            (#xx:3)
                // Only the 0x7x rows existed, without the inverting forms; 0x6x fell to a
                // silent "skipped". MEASURED: the firmware's page-refresh edge detector at
                // 0x0104F6 is `BLD #1,@FFF406 / BXOR #4,@FFF406 / BCC` ... `BLD #1 / BST
                // #4,@FFF406` - BST never ran, bit 4 never caught up with bit 1, and from
                // the first time bit 1 changed (leaving the demo) the main loop cleared and
                // redrew the page on EVERY pass, so the LCD task never got to send it.
                // Flags as for the 0x7C-0x7F group (BUG56): BTST writes Z only; BLD, BOR,
                // BXOR, BAND write C only; BSET/BNOT/BCLR/BST/BIST touch no flag.
                const bool    invert = (immByte & 0x80) != 0;
                const uint8_t bitnum = ((bitop & 0xF0) == 0x60 && (bitop & 0x0F) != 0x7)
                                     ? uint8_t(getRegisterValue(regs, (immByte >> 4) & 0x0F, 0) & 0x07)
                                     : uint8_t((immByte >> 4) & 0x07);
                uint8_t mem = emulator->readByte(addr);
                const bool bit = ((mem >> bitnum) & 1u) != 0;
                const bool src = invert ? !bit : bit;
                const char* opname = "BIT?";
                switch (bitop) {
                    case 0x60: case 0x70: mem |=  uint8_t(1u << bitnum); emulator->writeByte(addr, mem); opname = "BSET"; break;
                    case 0x61: case 0x71: mem ^=  uint8_t(1u << bitnum); emulator->writeByte(addr, mem); opname = "BNOT"; break;
                    case 0x62: case 0x72: mem &= uint8_t(~(1u << bitnum)); emulator->writeByte(addr, mem); opname = "BCLR"; break;
                    case 0x63: case 0x73: flags.zero = !bit; opname = "BTST"; break;
                    case 0x67: {
                        const bool c = invert ? !flags.carry : flags.carry;
                        mem = uint8_t((mem & ~(1u << bitnum)) | (uint8_t(c ? 1u : 0u) << bitnum));
                        emulator->writeByte(addr, mem); opname = invert ? "BIST" : "BST"; break;
                    }
                    case 0x74: flags.carry = flags.carry || src;  opname = invert ? "BIOR"  : "BOR";  break;
                    case 0x75: flags.carry = (flags.carry != src); opname = invert ? "BIXOR" : "BXOR"; break;
                    case 0x76: flags.carry = flags.carry && src;  opname = invert ? "BIAND" : "BAND"; break;
                    case 0x77: flags.carry = src;                 opname = invert ? "BILD"  : "BLD";  break;
                    default:
                        printf("[BITOP-UNKNOWN] 0x%06X: 6A %02X ... %02X %02X - operation byte %02X is not a "
                               "row of the @aa bit group (RENDERED pp.758-762)\n", base, emulator->readByte(base + 1),
                               bitop, immByte, bitop);
                        emulator->handleIllegalInstruction();
                        return true;
                }
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s #%d, @0x%06X (mem now 0x%02X)\n", base, opname, bitnum, addr, emulator->readByte(addr));
                break;
            }
            default: {
                printf("[ERROR] Unimplemented 0x6A operation 0x%X at PC 0x%06X\n",
                        operation, base);
                emulator->handleIllegalInstruction();
                return true;
            }
        }

        return true;
    }

    // ===== REGISTER OPERATIONS EXECUTOR IMPLEMENTATIONS =====

    // Handle 0x20 instruction (ADD #imm8, Rn)
    bool H8S2350InstructionExecutor::execute0x20Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (high nibble) and register index (low nibble)
        uint8_t immediate = (operand >> 4) & 0x0F;  // Immediate value (0-15)
        uint8_t rn = operand & 0x0F;               // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in 0x20 instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get current register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Perform addition
        uint16_t result = (uint16_t)reg_value + (uint16_t)immediate;
        uint8_t final_result = result & 0xFF;

        // Update flags
        flags.zero = (final_result == 0);
        flags.negative = (final_result & 0x80) != 0;
        flags.carry = (result > 0xFF);  // Carry if result > 255
        flags.overflow = ((reg_value & 0x80) == (immediate & 0x80) && (final_result & 0x80) != (reg_value & 0x80));

        // Store result in register
        regs.rl[rn] = final_result;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADD.B #0x%02X, R%d (0x%02X + 0x%02X = 0x%02X, Z=%d N=%d C=%d V=%d)\n",
               pc - instruction.size, immediate, rn, reg_value, immediate, final_result,
               flags.zero, flags.negative, flags.carry, flags.overflow);

        return true;
    }

    // Handle 0x21 instruction (ADD Rm, Rn)
    bool H8S2350InstructionExecutor::execute0x21Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register indices from operand byte
        uint8_t rm = (operand >> 4) & 0x0F;  // Source register (0-7)
        uint8_t rn = operand & 0x0F;         // Destination register (0-7)

        // Validate register indices
        if (rm >= 8 || rn >= 8) {
            printf("[ERROR] Invalid register indices in 0x21 instruction: rm=%d rn=%d at PC 0x%06X\n", rm, rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register values
        uint8_t src_value = regs.r[rm] & 0xFF;
        uint8_t dst_value = regs.r[rn] & 0xFF;

        // Perform addition
        uint16_t result = (uint16_t)dst_value + (uint16_t)src_value;
        uint8_t final_result = result & 0xFF;

        // Update flags
        flags.zero = (final_result == 0);
        flags.negative = (final_result & 0x80) != 0;
        flags.carry = (result > 0xFF);
        flags.overflow = ((dst_value & 0x80) == (src_value & 0x80) && (final_result & 0x80) != (dst_value & 0x80));

        // Store result in destination register
        regs.rl[rn] = final_result;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADD.B R%d, R%d (0x%02X + 0x%02X = 0x%02X, Z=%d N=%d C=%d V=%d)\n",
               pc - instruction.size, rm, rn, dst_value, src_value, final_result,
               flags.zero, flags.negative, flags.carry, flags.overflow);

        return true;
    }

    // Handle 0x28 instruction (CMP #imm8, Rn)
    bool H8S2350InstructionExecutor::execute0x28Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (high nibble) and register index (low nibble)
        uint8_t immediate = (operand >> 4) & 0x0F;  // Immediate value (0-15)
        uint8_t rn = operand & 0x0F;               // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in 0x28 instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Perform comparison (reg_value - immediate)
        uint16_t result = (uint16_t)reg_value - (uint16_t)immediate;
        uint8_t final_result = result & 0xFF;

        // Update flags (don't store result)
        flags.zero = (final_result == 0);
        flags.negative = (final_result & 0x80) != 0;
        flags.carry = (reg_value < immediate);  // Borrow occurred
        flags.overflow = ((reg_value & 0x80) != (immediate & 0x80) && (final_result & 0x80) == (immediate & 0x80));

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: CMP.B #0x%02X, R%d (0x%02X - 0x%02X = 0x%02X, Z=%d N=%d C=%d V=%d)\n",
               pc - instruction.size, immediate, rn, reg_value, immediate, final_result,
               flags.zero, flags.negative, flags.carry, flags.overflow);

        return true;
    }

    // Handle 0x30 instruction (AND #imm8, Rn)
    bool H8S2350InstructionExecutor::execute0x30Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (high nibble) and register index (low nibble)
        uint8_t immediate = (operand >> 4) & 0x0F;  // Immediate value (0-15)
        uint8_t rn = operand & 0x0F;               // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in 0x30 instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Perform AND operation
        uint8_t result = reg_value & immediate;

        // Update flags
        flags.zero = (result == 0);
        flags.negative = (result & 0x80) != 0;
        flags.carry = false;  // AND doesn't affect carry
        flags.overflow = false;  // AND doesn't affect overflow

        // Store result in register
        regs.rl[rn] = result;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: AND.B #0x%02X, R%d (0x%02X & 0x%02X = 0x%02X, Z=%d N=%d)\n",
               pc - instruction.size, immediate, rn, reg_value, immediate, result,
               flags.zero, flags.negative);

        return true;
    }

    // Handle 0x38 instruction (SHLL Rn)
    bool H8S2350InstructionExecutor::execute0x38Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register index from operand byte
        uint8_t rn = operand & 0x0F;  // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in 0x38 instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Perform logical shift left
        uint8_t result = reg_value << 1;

        // Update flags
        flags.zero = (result == 0);
        flags.negative = (result & 0x80) != 0;
        flags.carry = (reg_value & 0x80) != 0;  // MSB becomes carry
        flags.overflow = false;  // SHLL doesn't affect overflow

        // Store result in register
        regs.rl[rn] = result;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: SHLL.B R%d (0x%02X << 1 = 0x%02X, Z=%d N=%d C=%d)\n",
               pc - instruction.size, rn, reg_value, result,
               flags.zero, flags.negative, flags.carry);

        return true;
    }

    // Handle 0x40 instruction (ADDQ #imm3, Rn)
    bool H8S2350InstructionExecutor::execute0x40Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t rn = operand & 0x07;               // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in 0x40 instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get current register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Perform addition
        uint16_t result = (uint16_t)reg_value + (uint16_t)immediate;
        uint8_t final_result = result & 0xFF;

        // Update flags
        flags.zero = (final_result == 0);
        flags.negative = (final_result & 0x80) != 0;
        flags.carry = (result > 0xFF);
        flags.overflow = ((reg_value & 0x80) == (immediate & 0x80) && (final_result & 0x80) != (reg_value & 0x80));

        // Store result in register
        regs.rl[rn] = final_result;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADDQ #0x%X, R%d (0x%02X + 0x%02X = 0x%02X, Z=%d N=%d C=%d V=%d)\n",
               pc - instruction.size, immediate, rn, reg_value, immediate, final_result,
               flags.zero, flags.negative, flags.carry, flags.overflow);

        return true;
    }

    // Handle 0x46 instruction (MOV Rm, Rn)
    bool H8S2350InstructionExecutor::execute0x46Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register indices from operand byte
        uint8_t rm = (operand >> 4) & 0x0F;  // Source register (0-7)
        uint8_t rn = operand & 0x0F;         // Destination register (0-7)

        // Validate register indices
        if (rm >= 8 || rn >= 8) {
            printf("[ERROR] Invalid register indices in 0x46 instruction: rm=%d rn=%d at PC 0x%06X\n", rm, rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Move byte from source register to destination register
        uint8_t value = regs.r[rm] & 0xFF;
        regs.rl[rn] = value;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        // Update flags
        flags.zero = (value == 0);
        flags.negative = (value & 0x80) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.B R%d, R%d (value=0x%02X, Z=%d N=%d)\n",
               pc - instruction.size, rm, rn, value, flags.zero, flags.negative);

        return true;
    }

    // Handle 0x47 instruction (MOV.L ERm, ERn)
    bool H8S2350InstructionExecutor::execute0x47Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register indices from operand byte
        uint8_t erm = (operand >> 4) & 0x0F;  // Source ER register (0-7)
        uint8_t ern = operand & 0x0F;         // Destination ER register (0-7)

        // Validate register indices
        if (erm >= 8 || ern >= 8) {
            printf("[ERROR] Invalid register indices in 0x47 instruction: erm=%d ern=%d at PC 0x%06X\n", erm, ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Move long from source ER register to destination ER register
        uint32_t value = regs.er[erm];
        regs.er[ern] = value;
        emulator->syncRegAfterLongWrite(ern);

        // Update flags
        flags.zero = (value == 0);
        flags.negative = (value & 0x80000000) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.L ER%d, ER%d (value=0x%08X, Z=%d N=%d)\n",
               pc - instruction.size, erm, ern, value, flags.zero, flags.negative);

        return true;
    }

    // Handle 0x54 instruction (RTS - Return from Subroutine) - FIXED from INC.L
    bool H8S2350InstructionExecutor::execute0x54Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Debug: Show stack state before RTS
        if (!g_h8s_quiet_boot) printf("[DEBUG-RTS] SP=0x%06X before pop\n", regs.sp);

        // Pop return address from stack (24-bit)
        uint32_t return_address = emulator->pop24() & 0x00FFFFFF;

        // 2026-09-16: gated. This was an UNCONDITIONAL printf on every RTS, and once the
        // boot started running properly it produced 376,254 lines in the first 400,000 -
        // 94% of a 5.5-million-line log, hiding everything else and slowing the run.
        // AN INSTRUMENT ON AN ALWAYS-RUNNING PATH IS ITSELF AN INTERVENTION - the rule
        // this thread already paid for on the S3000XL, where per-access probes slowed the
        // emulation enough that Thor could HEAR it.
        if (!g_h8s_quiet_boot)
            printf("[RTS-EXEC] pc=0x%06X return_address=0x%06X\n", pc, return_address);

        // Stack corruption safety check
        if (return_address == 0x000000 || return_address < 0x000800) {
            printf("[STACK-ERROR] RTS returned to vector table! PC=0x%06X\n", return_address);
            printf("[STACK-DUMP] Current SP=0x%06X\n", regs.sp);
            // Don't abort - let it continue for debugging
        }
        
        // HACK: Special case for unknown functions with return_address=0

        // Stack corruption safety check
        if (return_address == 0x000000 || return_address < 0x000800) {
            printf("[STACK-ERROR] RTS returned to vector table! PC=0x%06X\n", return_address);
            printf("[STACK-DUMP] Current SP=0x%06X\n", regs.sp);
            // Don't abort - let it continue for debugging
        }

        // Set new PC (this will override the default PC increment)
        regs.pc = return_address;

        if (!g_h8s_quiet_boot) printf("[DEBUG-RTS] New PC=0x%06X after RTS\n", return_address);
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: RTS -> PC=0x%06X (from stack)\n",
               pc - instruction.size, return_address);

        return true;
    }

    // Handle 0x59 instruction (ROTXL - Rotate Left Through Carry)
    // ===================================================================
    // BUG82, 2026-09-17 - THIS BODY IS A FABRICATION TWICE OVER, AND IT IS
    // NO LONGER DISPATCHED. Kept compiled out so the shape stays on record.
    //
    //   (1) 0x59 IS `JMP @ERn` - `5 9 | 0:ern 0`, RENDERED PDF page 800
    //       (printed "764"), two bytes, no condition code affected. It is
    //       decoded and executed beside its sibling `JSR @ERn` (0x5D) now.
    //   (2) THERE IS NO REGISTER-COUNT ROTATE ON THIS PART AT ALL. Every
    //       rotate row in Appendix A.2 carries a FIXED count encoded in the
    //       instruction - RENDERED PDF page 803 (printed "767") shows
    //       `ROTL.B Rd = 1 2 | 8 rd` and `ROTL.B #2,Rd = 1 2 | C rd`, with
    //       the .W and .L rows in the same primary. This body read a COUNT
    //       OUT OF A REGISTER and looped on it - an instruction the silicon
    //       does not have, on an opcode that belongs to something else.
    //
    // And it was not inert. The decoder left 0x59 undecoded, so the fallback
    // handed the executor `opcode = 0x59, size = 2` and this ran: it rotated
    // R0's low byte by whatever R0 happened to hold and rewrote C, Z, N and V
    // - while the jump the firmware asked for did not happen. Measured after
    // BUG81 opened the path: five `59` sites, then a walk into PC 0xF80004.
    // THE NINTH TIME A FICTION HAS DEFENDED ITSELF WITH A C2196 DUPLICATE-CASE
    // ERROR against the real instruction (`error C2196: case value '89'
    // already used`).
    // ===================================================================
#if 0
    bool H8S2350InstructionExecutor::execute0x59Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // ROTXL: secondary byte = (rd << 4) | rs
        // rd = destination register, rs = source register (count)
        if (instruction.size == 2) {
            uint8_t operand = emulator->readByte(pc - instruction.size + 1);
            uint8_t rd = (operand >> 4) & 0x0F;
            uint8_t rs = operand & 0x0F;
            
            if (rd >= 8 || rs >= 8) {
                emulator->handleIllegalInstruction();
                return true;
            }
            
            uint8_t count = regs.r[rs] & 0xFF;
            uint8_t value = regs.r[rd] & 0xFF;
            
            // Rotate left through carry by count modulo 8
            for (uint8_t i = 0; i < (count % 8); i++) {
                uint8_t new_carry = (value & 0x80) != 0;
                value = (value << 1) | (flags.carry ? 1 : 0);
                flags.carry = new_carry;
            }
            
            regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | value;
            
            flags.zero = (value == 0);
            flags.negative = (value & 0x80) != 0;
            // carry already set in loop
            flags.overflow = false;  // rotation doesn't set overflow
            
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ROTXL.B R%d, R%d (cnt=%d) -> 0x%02X C=%d Z=%d N=%d\n",
                   pc - instruction.size, rd, rs, count, value, flags.carry, flags.zero, flags.negative);
        }
        return true;
    }
#endif

    // Handle 0x5A instruction (SHAR - Shift Arithmetic Right)
    bool H8S2350InstructionExecutor::execute0x5AInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        
        if (instruction.size == 2) {
            uint8_t operand = emulator->readByte(pc - instruction.size + 1);
            uint8_t rd = (operand >> 4) & 0x0F;
            uint8_t rs = operand & 0x0F;
            
            if (rd >= 8 || rs >= 8) {
                emulator->handleIllegalInstruction();
                return true;
            }
            
            uint8_t count = regs.r[rs] & 0xFF;
            int8_t value = static_cast<int8_t>(regs.r[rd] & 0xFF);
            
            // Arithmetic shift right (preserve sign bit)
            for (uint8_t i = 0; i < (count % 8); i++) {
                flags.carry = (value & 0x01) != 0;
                value = (value >> 1) | (value & 0x80);  // preserve sign bit
            }
            
            regs.r[rd] = (regs.r[rd] & 0xFFFFFF00u) | static_cast<uint8_t>(value);
            
            flags.zero = (value == 0);
            flags.negative = (value & 0x80) != 0;
            flags.overflow = false;
            
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: SHAR.B R%d, R%d (cnt=%d) -> 0x%02X C=%d Z=%d N=%d\n",
                   pc - instruction.size, rd, rs, count, value, flags.carry, flags.zero, flags.negative);
        }
        return true;
    }

    // Handle 0x56 instruction (RTE or PUSH Rn - distinguish by second byte)
    bool H8S2350InstructionExecutor::execute0x56Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        // Check second byte to distinguish RTE (0x56 0x70) from PUSH Rn (0x56 0xXX)
        uint32_t pc = emulator->getProgramCounter();
        uint8_t secondByte = emulator->readByte(pc - instruction.size + 1);
        
        if (secondByte == 0x70) {
            // RTE - Return from Exception (0x56 0x70)
            return emulator->executeRTE();
        }
        else if (instruction.size == 2) {
            // PUSH Rn (2 byte instruction)
            auto& regs = emulator->getRegisters();
            uint32_t pc = emulator->getProgramCounter();

            // Read the operand byte to get register index
            uint8_t operand = emulator->readByte(pc - instruction.size + 1);

            // Extract register index from operand byte
            uint8_t rn = operand & 0x0F;  // Register index (0-7)

            // Validate register index
            if (rn >= 8) {
                printf("[ERROR] Invalid register index %d in 0x56 instruction at PC 0x%06X\n", rn, pc - instruction.size);
                emulator->handleIllegalInstruction();
                return true;
            }

            // Get register value
            uint8_t value = regs.r[rn] & 0xFF;

            // Push byte onto stack
            emulator->pushByte(value);

            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: PUSH.B R%d (value=0x%02X, SP=0x%08X)\n",
                   pc - instruction.size, rn, value, regs.er[7]);

            return true;
        }
        else {
            printf("[ERROR] Invalid instruction size %d for 0x56 at PC 0x%06X\n", 
                   instruction.size, emulator->getProgramCounter() - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }
    }

    // Handle 0x58 instruction (POP Rn)
    bool H8S2350InstructionExecutor::execute0x58Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register index from operand byte
        uint8_t rn = operand & 0x0F;  // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in 0x58 instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Pop byte from stack
        uint8_t value = emulator->popByte();

        // Store in register
        regs.rl[rn] = value;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        // Update flags
        flags.zero = (value == 0);
        flags.negative = (value & 0x80) != 0;
        flags.carry = false;  // POP doesn't affect carry
        flags.overflow = false;  // POP doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: POP.B R%d (value=0x%02X, SP=0x%08X, Z=%d N=%d)\n",
               pc - instruction.size, rn, value, regs.er[7], flags.zero, flags.negative);

        return true;
    }

    // Handle 0x5C instruction (MOV.W @ERm, Rn)
    bool H8S2350InstructionExecutor::execute0x5CInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        // BUG58, 2026-09-16 - 0x5C IS `BSR d:16`, AND IT NEVER BRANCHED.
        //
        // The DECODER has said `instr.mnemonic = "BSR d:16"`, size 4, for a long time.
        // This executor implemented `MOV.W @ERm, Rn` - a different instruction entirely,
        // and a fictional one on this opcode. It read `operand = byte[start+1]`, which on
        // the real encoding is the mandatory `0x00`, so it took erm = 0 and rn = 0, loaded
        // a word from whatever ER0 happened to hold into R0, and **fell through to the
        // next instruction**. THE SUBROUTINE WAS NEVER CALLED.
        //
        // This is BUG39's shape exactly - the decoder and the executor disagreeing in
        // silence - and `--quiet-boot` suppressed the one printf that would have said
        // "MOV.W @ER0, R0" on an instruction the disassembly calls BSR.
        //
        // MEASURED: the RAM-resident main loop's first instruction is
        //     407222: 5C 00 06 66    BSR -> 0x40788C
        // and `MS2K_LOOPWATCH=0x40788C` recorded **ZERO visits** while the loop itself ran
        // millions of times. `0x40788C` is the firmware's 1 kHz TICK - it polls TSR2 bit 0
        // (TGFA, the TPU channel-2 compare-match flag) and, when set, drives the LED latch
        // at 0x00200000 and P1DR. **None of that had ever executed.** The idle loop read
        // SSR1 1,184,266 times in two seconds because the tick that gives it work was
        // being skipped.
        //
        // Renesas HM Rev 3.00 Appendix A.1, RENDERED PDF page 798 (printed "762"):
        //     BSR d:8    5 5 | disp             2 bytes
        //     BSR d:16   5 C | 0 0 | disp:16    4 bytes
        // The displacement is SIGNED and relative to the address of the NEXT instruction.
        // No condition code is affected.
        auto& regs = emulator->getRegisters();
        const uint32_t pc    = emulator->getProgramCounter();
        const uint32_t start = (pc - instruction.size) & 0x00FFFFFFu;
        const uint8_t  b1    = emulator->readByte(start + 1);
        if (b1 != 0x00) {
            printf("[BSR16-UNKNOWN] 0x%06X: bytes 5C %02X - the second byte of BSR d:16 is "
                   "always 0x00 (RENDERED page 798)\n", start, b1);
            emulator->halt();
            return false;
        }
        const int16_t  disp   = (int16_t)((uint16_t(emulator->readByte(start + 2)) << 8)
                                        |  uint16_t(emulator->readByte(start + 3)));
        const uint32_t ret    = (start + 4) & 0x00FFFFFFu;          // next instruction
        const uint32_t target = (ret + uint32_t(int32_t(disp))) & 0x00FFFFFFu;

        emulator->push24(ret);
        regs.pc = target;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BSR d:16 -> 0x%06X (disp=%d, ret=0x%06X)\n",
               start, target, int(disp), ret);
        emulator->addCycles(4);
        return true;
    }

    // Handle 0x5E instruction (JSR @aa:24)
    bool H8S2350InstructionExecutor::execute0x5EInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // JSR @aa:24  (5E aa-hi aa-mid aa-lo) - Renesas H8S/2600 p.136
        // Read the 24-bit absolute target from bytes 2..4 of this instruction.
        uint32_t base = pc - instruction.size;  // address of the 0x5E byte
        uint32_t target = ((uint32_t)emulator->readByte(base + 1) << 16) |
                          ((uint32_t)emulator->readByte(base + 2) << 8) |
                          ((uint32_t)emulator->readByte(base + 3));
        target &= 0x00FFFFFF;

        // Push the return address (PC already points past this 4-byte instruction).
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: JSR @0x%06X (push return 0x%06X, SP=0x%06X)\n",
               base, target, pc, regs.sp);
        emulator->push24(pc);          // 32-bit frame, alias-safe (push24 handles SP)
        emulator->setProgramCounter(target & 0x00FFFFFF);

        return true;
    return true;
    }

    // Handle 0x60 instruction (ANDC #imm8, CCR)
    bool H8S2350InstructionExecutor::execute0x60Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // BUG38, 2026-09-13 - THIS WAS NOT ANDC AND IT CORRUPTED THE CCR.
        // ANDC #xx:8,CCR is `0 6 | IMM` (RENDERED PDF page 794, printed "758"), which
        // is now decoded and executed correctly in the 0x04-0x07 group. 0x60 is
        // `BSET Rn,Rd` - a different instruction entirely, and one that affects NO
        // condition codes at all.
        //
        // What stood here did three wrong things at once: it claimed to be ANDC on the
        // wrong opcode, it took the immediate as `operand & 0x0F` - a FOUR-bit mask
        // where ANDC's immediate is the whole second byte - and it then assigned that
        // mask straight into `regs.ccr`, wiping the top bits and leaving the flags
        // struct stale. Any firmware BSET reaching here would have destroyed the
        // condition codes silently.
        //
        // The first build after removing the fiction halted here immediately -
        // `[OPCODE-0x60-FICTION] 0x004614: bytes 60 C0` - so the firmware DOES execute
        // this opcode, and the old handler really was writing a 4-bit mask into the CCR
        // on a live path.
        //
        // BSET Rn,Rd, Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED PDF page 798
        // (printed "page 762 of 988"):
        //
        //     BSET Rn,Rd     B     6 0 | rn rd        2 bytes
        //
        // `rn` and `rd` are the EIGHT-bit register field, so four bits each and
        // 1000-1111 are R0L..R7L (legend, rendered page 771 - BUG29's field). The bit
        // number is the lower three bits of Rn8. NO condition code is affected: BSET
        // writes a bit, it does not compute flags. BTST is the one that writes Z.
        const uint32_t at  = pc - instruction.size;
        const uint8_t  op2 = emulator->readByte(at + 1);
        const uint8_t  rn  = (op2 >> 4) & 0x0F;
        const uint8_t  rd  =  op2       & 0x0F;
        const uint8_t  bit = (uint8_t)(getRegisterValue(regs, rn, 0) & 0x07);
        const uint8_t  before = (uint8_t)getRegisterValue(regs, rd, 0);
        // BUG57, 2026-09-16: the register-to-register bit group is FOUR consecutive
        // primaries on the same page, not one. RENDERED page 798 (printed "762"):
        //     BSET Rn,Rd  6 0 | rn rd      BNOT Rn,Rd  6 1 | rn rd
        //     BCLR Rn,Rd  6 2 | rn rd      BTST Rn,Rd  6 3 | rn rd
        // all two bytes, and NO condition code is affected by the first three (BTST,
        // which writes Z only, has its own case). Only 0x60 and 0x63 were implemented,
        // so `[OPCODE-MISSING] 0x0062 at PC 0x01215A, raw bytes [62 98]` = BCLR R1L,R0L
        // halted the normal boot path.
        const uint8_t  after  = (instruction.opcode == 0x60) ? uint8_t(before |  (1u << bit))
                              : (instruction.opcode == 0x61) ? uint8_t(before ^  (1u << bit))
                                                             : uint8_t(before & ~(1u << bit));
        setRegisterValue(regs, rd, after, 0);

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s R%d%c(bit %d),R%d%c (0x%02X -> 0x%02X)\n",
               at, (instruction.opcode == 0x60) ? "BSET" : (instruction.opcode == 0x61) ? "BNOT" : "BCLR",
               rn & 7, (rn & 8) ? 'L' : 'H', bit, rd & 7, (rd & 8) ? 'L' : 'H', before, after);
        emulator->addCycles(2);
        return true;
    }

    // Handle 0x6B instruction (LDMS ERd, @ERs)
    bool H8S2350InstructionExecutor::execute0x6BInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;     // address of the 0x6B byte
        uint8_t operation = (uint8_t)instruction.source_operand;
        // BUG110: the register field is 4 bits - 0-7 = R0-R7, 8-15 = E0-E7 (HM Rev 3.00 Appendix A,
        // register field legend, rendered page 807). It was masked to 3 bits, so every MOV.W
        // @aa,En loaded Rn instead and every MOV.W En,@aa stored Rn.
        const uint8_t reg_field = (uint8_t)instruction.destination_operand & 0x0F;
        const uint8_t reg_idx   = reg_field & 0x07;
        const bool    isE       = (reg_field & 0x08) != 0;
        auto loadW = [&](uint16_t v) {
            if (isE) { regs.e[reg_idx] = v; emulator->syncRegAfterUpperWordWrite(reg_idx); }
            else     { regs.r[reg_idx] = v; emulator->syncRegAfterWordWrite(reg_idx); }
        };

        switch (operation) {
            case 0x0: { // MOV.W @aa:16, Rd
                uint16_t addr16 = (uint16_t)((emulator->readByte(base+2)<<8) | emulator->readByte(base+3));
                uint32_t addr = (addr16 & 0x8000) ? (0xFFFF0000u | addr16) : addr16;
                uint16_t val = (uint16_t)((emulator->readByte(addr & 0x00FFFFFF)<<8) | emulator->readByte((addr+1) & 0x00FFFFFF));
                loadW(val);
                flags.zero = (val==0); flags.negative = (val & 0x8000)!=0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W @0x%04X, R%d = 0x%04X\n", base, addr16, reg_idx, val);
                break;
            }
            case 0x2: { // MOV.W @aa:32, Rd
                uint32_t addr = ((uint32_t)emulator->readByte(base+2)<<24)|((uint32_t)emulator->readByte(base+3)<<16)|((uint32_t)emulator->readByte(base+4)<<8)|emulator->readByte(base+5);
                addr &= 0x00FFFFFF;
                uint16_t val = (uint16_t)((emulator->readByte(addr)<<8) | emulator->readByte((addr+1)&0x00FFFFFF));
                loadW(val);
                flags.zero = (val==0); flags.negative = (val & 0x8000)!=0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W @0x%08X, R%d = 0x%04X\n", base, addr, reg_idx, val);
                break;
            }
            case 0x8: { // MOV.W Rs, @aa:16
                uint16_t addr16 = (uint16_t)((emulator->readByte(base+2)<<8) | emulator->readByte(base+3));
                uint32_t addr = (addr16 & 0x8000) ? (0xFFFF0000u | addr16) : addr16;
                uint16_t val = isE ? regs.e[reg_idx] : regs.r[reg_idx];
                emulator->storeWord(addr & 0x00FFFFFF, val);   // BUG126: one bus cycle into CS0
                flags.zero = (val==0); flags.negative = (val & 0x8000)!=0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W R%d=0x%04X -> @0x%04X\n", base, reg_idx, val, addr16);
                break;
            }
            case 0xA: { // MOV.W Rs, @aa:32
                uint32_t addr = ((uint32_t)emulator->readByte(base+2)<<24)|((uint32_t)emulator->readByte(base+3)<<16)|((uint32_t)emulator->readByte(base+4)<<8)|emulator->readByte(base+5);
                addr &= 0x00FFFFFF;
                uint16_t val = isE ? regs.e[reg_idx] : regs.r[reg_idx];
                emulator->storeWord(addr, val);   // BUG126: one bus cycle into CS0
                flags.zero = (val==0); flags.negative = (val & 0x8000)!=0; flags.overflow = false;
                if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: MOV.W R%d=0x%04X -> @0x%08X\n", base, reg_idx, val, addr);
                break;
            }
            default:
                printf("[ERROR] Unimplemented 0x6B operation 0x%X at PC 0x%06X\n", operation, base);
                emulator->handleIllegalInstruction();
                return true;
        }
        return true;
    }

    // Handle 0x6D instruction (MOVU.W @ERm+, Rn)
    bool H8S2350InstructionExecutor::execute0x6DInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;

        // ---------------------------------------------------------------------
        // BUG34, 2026-09-13, STACK-0x1CE6E. THE LONGWORD FORM OF THIS GROUP WAS
        // NOT IMPLEMENTED, AND FELL INTO THE WORD HANDLER BELOW READING THE
        // PREFIX BYTE AS ITS OPERAND.
        //
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED page 803 of the
        // PDF (printed "page 767"), read against the mnemonic column on the same
        // page - the text extract agrees, but the encoding came off the image:
        //
        //   MOV.L @ERs+,ERd   L   0 1 | 0 0 | 6 D | 0:ers 0:erd     4 bytes
        //   MOV.L ERs,@-ERd   L   0 1 | 0 0 | 6 D | 1:erd 0:ers     4 bytes
        //   POP.L  ERn        L   0 1 | 0 0 | 6 D | 7     0:ern     ( = MOV.L @SP+,ERn )
        //   PUSH.L ERn        L   0 1 | 0 0 | 6 D | F     0:ern     ( = MOV.L ERn,@-SP )
        //
        // `base + 1` is the prefix's own `00` on this form, so `01 00 6D F0`
        // (PUSH.L ER0) was executed with operand 0x00 = "MOV.W @ER0+,R0": it
        // MOVED THE STACK POINTER NOT AT ALL and corrupted ER0 and R0 instead.
        // Measured at 0x01CE04 with MS2K_SPTRACE: SP stayed 0xFFFBF8 across the
        // push, and its matching POP.L at 0x01CE3C then took the stack apart.
        //
        // The label on `case 0x6D` said "MOVU.W @ERm+, Rn". MOVU is an H8SX
        // instruction and has ZERO hits in this manual - the same test that
        // condemned MOVA.L (FIX27c), MOVU.L at 0x6F (BUG24) and the invented
        // "firmware-specific 0x01 MOV.B" (BUG27). FOURTH member of that family.
        // ---------------------------------------------------------------------
        const bool isLong = (instruction.size >= 4)
                         && (emulator->readByte(base + 0) == 0x01)
                         && (emulator->readByte(base + 1) == 0x00)
                         && (emulator->readByte(base + 2) == 0x6D);

        if (isLong) {
            const uint8_t b4     = emulator->readByte(base + 3);
            const bool    store  = (b4 & 0x80) != 0;      // 1:erd = pre-decrement store
            const uint8_t erAddr = (b4 >> 4) & 0x07;      // ERs (load) / ERd (store); 7 = SP
            const uint8_t erData =  b4       & 0x07;      // ERd (load) / ERs (store)

            uint32_t value;
            if (!store) {
                const uint32_t address = regs.er[erAddr] & 0x00FFFFFFu;   // BUG122: 24-bit address
                value = (uint32_t(emulator->readByte(address + 0)) << 24)
                      | (uint32_t(emulator->readByte(address + 1)) << 16)
                      | (uint32_t(emulator->readByte(address + 2)) <<  8)
                      |  uint32_t(emulator->readByte(address + 3));
                regs.er[erAddr] = (regs.er[erAddr] + 4) & 0xFFFFFFFFu;
                emulator->syncRegAfterLongWrite(erAddr);
                regs.er[erData] = value;
                emulator->syncRegAfterLongWrite(erData);
            } else {
                value = regs.er[erData] & 0xFFFFFFFFu;
                regs.er[erAddr] = (regs.er[erAddr] - 4) & 0xFFFFFFFFu;
                emulator->syncRegAfterLongWrite(erAddr);
                const uint32_t address = regs.er[erAddr] & 0x00FFFFFFu;   // BUG122: 24-bit address
                emulator->storeLong(address, value);   // BUG126: two x16 bus cycles into CS0
            }
            // MOV flags, HM Appendix A.1 / the MOV page: N and Z from the value,
            // V cleared, C UNCHANGED. Do not touch carry here.
            flags.zero     = (value == 0);
            flags.negative = (value & 0x80000000u) != 0;
            flags.overflow = false;
            if (!g_h8s_quiet_boot) {
                const char* nm = store ? ((erAddr == 7) ? "PUSH.L" : "MOV.L ERs,@-ERd")
                                       : ((erAddr == 7) ? "POP.L"  : "MOV.L @ERs+,ERd");
                printf("[EXECUTE] 0x%06X: %s ER%d (ER%d=0x%08X, value=0x%08X)\n",
                       base, nm, erData, erAddr, regs.er[erAddr], value);
            }
            emulator->addCycles(H8S_CYC_BASE_MEM);
            return true;
        }

        uint8_t operand = emulator->readByte(base + 1);

        // 0x6D forms (Renesas h8s2600 p.156/162/185/286):
        //   6D 0 ers rd = MOV.W @ERs+, Rd  (load, post-increment)   - top nibble 0
        //   6D 1 erd rs = MOV.W Rs, @-ERd  (store, pre-decrement)    - top nibble 1
        //   6D 7 rn     = POP.W  Rn        (= MOV.W @ER7+, Rn)       - top nibble 7
        //   6D F rn     = PUSH.W Rn        (= MOV.W Rn, @-ER7)       - top nibble F
        // The high bit (0x08) of the top nibble selects STORE(push/pre-dec) vs LOAD(pop/post-inc).
        // ---------------------------------------------------------------------
        // BUG80, 2026-09-17 - THE LINE BELOW USED TO OVERWRITE ITS OWN CORRECT
        // ANSWER, AND IT WAS ENDING EVERY RUN THIS PROJECT HAS MADE SINCE BUG74.
        //
        //     bool isStore = (topNib & 0x08) != 0;     // correct
        //     ...
        //     isStore = (topNib == 0x1);               // <- and then this
        //
        // RENDERED PDF page 802 (printed "page 766 of 988"), Appendix A.1:
        //
        //     MOV.W @ERs+,Rd   W   6 D | 0:ers rd   2 bytes
        //     MOV.W Rs,@-ERd   W   6 D | 1:erd rs   2 bytes
        //
        // The field is `0:ers` / `1:erd` - ONE BIT, bit 7 of the second byte, which
        // is the TOP NIBBLE'S HIGH BIT and not the whole nibble. So nibbles 0-7 are
        // loads through ER0-ER7 and 8-F are stores through ER0-ER7. Testing
        // `topNib == 0x1` gets EIGHT of the sixteen rows wrong:
        //
        //     nibble 1      MOV.W @ER1+,Rd   ->  executed as a pre-decrement STORE
        //     nibbles 8-E   MOV.W Rs,@-ERd   ->  executed as post-increment LOADS
        //
        // Only 0, 2-7 and F were right, and nibble 1 is the common one.
        //
        // WHAT IT COST, measured. The firmware's 650-byte copy at 0x00048A:
        //
        //     00048A  MOV.L #0x00000200, ER1      source, just past the vector table
        //     000490  MOV.L #0x00400000, ER2      destination, DRAM
        //     000496  MOV.W #0x028A, E0 / INC.W / SHLR.W   -> 325 words
        //     00049E  6D 10   MOV.W @ER1+, R0     <- executed as MOV.W R0,@-ER1
        //     0004A0  69 A0   MOV.W R0, @ER2
        //     0004A2  0B 82   ADDS #2, ER2
        //     0004A4  1B 58   DEC.W #1, E0
        //     0004A6  46 F6   BNE -> 0x00049E
        //
        // The PC ring caught it in one dump: ER1 walking DOWN 0x0C, 0x0A, 0x08 ...
        // 0x00, 0xFFFFFFFE while ER2 walked UP, and R0 FROZEN at 0x1347 because the
        // load never happened. So instead of reading flash into DRAM, the loop was
        // WRITING 0x1347 BACKWARDS THROUGH THE VECTOR TABLE at 0x000000-0x0001FF.
        //
        // Two boundaries later the ring shows SP drop by six and EXR's mask go 0 -> 2
        // - an interrupt accepted at TPU-ch2 level (IPRG = 0x20) - and the PC becomes
        // 0x471347, whose low half IS R0. **THE MACHINE JUMPED TO A VECTOR IT HAD
        // JUST OVERWRITTEN ITSELF**, landed on an odd address, and halted.
        // ---------------------------------------------------------------------
        const uint8_t topNib  = (operand >> 4) & 0x0F;
        const bool    isStore = (topNib & 0x08) != 0;   // bit 7: 0 = load/+, 1 = store/-
        const uint8_t erReg   = topNib & 0x07;          // ERs (load) / ERd (store); 7 = SP
        const uint8_t dataReg = operand & 0x07;         // Rd (load) / Rs (store)
        const bool    dataIsE = (operand & 0x08) != 0;  // BUG112: 16-bit field 8-15 = E0-E7 (HM A legend, rendered p.807) - was masked to R0-R7.

        if (!isStore) {
            // LOAD: value = mem[ER], then ER += 2 (post-increment / pop)
            // BUG122 (DIFFREF MOV oracle): the address is the LOW 24 BITS of ERn - the H8S/2350
            // has a 24-bit address bus and ignores the upper 8 bits of the effective address
            // (HM REJ09B0330 section 2 addressing modes). With a non-zero top byte this read
            // went off the map and returned 0xFFFF; the store below wrote nowhere.
            uint32_t address = regs.er[erReg] & 0x00FFFFFFu;
            uint16_t value = ((uint16_t)emulator->readByte(address) << 8)
                           |  (uint16_t)emulator->readByte(address + 1);
            regs.er[erReg] = (regs.er[erReg] + 2) & 0xFFFFFFFF;
            emulator->syncRegAfterLongWrite(erReg);
            if (dataIsE) { regs.e[dataReg] = value; emulator->syncRegAfterUpperWordWrite(dataReg); }
            else         { regs.r[dataReg] = value; emulator->syncRegAfterWordWrite(dataReg); }
            flags.zero = (value == 0);
            flags.negative = (value & 0x8000) != 0;
            flags.overflow = false;
            const char* nm = (topNib == 0x7) ? "POP.W" : "MOV.W @ERs+";
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s -> R%d (@0x%08X = 0x%04X, ER%d=0x%08X, Z=%d N=%d)\n",
                   base, nm, dataReg, address, value, erReg, regs.er[erReg], flags.zero, flags.negative);
        } else {
            // STORE: ER -= 2, then mem[ER] = value (pre-decrement / push)
            regs.er[erReg] = (regs.er[erReg] - 2) & 0xFFFFFFFF;
            emulator->syncRegAfterLongWrite(erReg);
            uint32_t address = regs.er[erReg] & 0x00FFFFFFu;   // BUG122: 24-bit address
            uint16_t value = dataIsE ? regs.e[dataReg] : (uint16_t)(regs.r[dataReg] & 0xFFFF);
            emulator->storeWord(address, value);   // BUG126: one bus cycle into CS0
            flags.zero = (value == 0);
            flags.negative = (value & 0x8000) != 0;
            flags.overflow = false;
            const char* nm = (topNib == 0xF) ? "PUSH.W" : "MOV.W Rs,@-ERd";
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: %s R%d (@0x%08X = 0x%04X, ER%d=0x%08X)\n",
                   base, nm, dataReg, address, value, erReg, regs.er[erReg]);
        }
        emulator->addCycles(H8S_CYC_BASE_MEM);
        return true;
    }
    bool H8S2350InstructionExecutor::execute0x6FInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs  = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        const uint32_t start = emulator->getProgramCounter() - instruction.size;

        // MOV.W with a 16-bit displacement. Renesas H8S/2350 HM Rev 3.00, Appendix
        // A.1, RENDERED pages 765 and 766:
        //     MOV.W @(d:16,ERs),Rd   6 F   0:ers rd   disp    bit 7 CLEAR = LOAD
        //     MOV.W Rs,@(d:16,ERd)   6 F   1:erd rs   disp    bit 7 SET   = STORE
        // The displacement is SIGNED 16-bit. Four bytes total.
        //
        // 2026-09-13: replaces "MOVU.L @ERm+,ERn" - an H8SX instruction with ZERO
        // hits in this manual, exactly like the MOVA.L fiction FIX27c removed.
        const uint8_t  op2  = emulator->readByte(start + 1);
        const bool     isStore = (op2 & 0x80) != 0;
        const uint8_t  ern  = (op2 >> 4) & 0x07;     // address register
        const uint8_t  rn   =  op2       & 0x07;     // data register
        const bool     rnIsE = (op2 & 0x08) != 0;     // BUG112: 16-bit field 8-15 = E0-E7 (HM A legend, rendered p.807) - was masked to R0-R7.
        const int16_t  disp = int16_t((uint16_t(emulator->readByte(start + 2)) << 8) |
                                       uint16_t(emulator->readByte(start + 3)));
        const uint32_t address = (regs.er[ern] + uint32_t(int32_t(disp))) & 0x00FFFFFF;

        uint16_t value;
        if (isStore) {
            value = rnIsE ? regs.e[rn] : uint16_t(regs.r[rn] & 0xFFFF);
            emulator->storeWord(address, value);   // BUG126: one bus cycle into CS0
            emulator->addCycles(H8S_CYC_BASE_MEM);
            emulator->addCycles(emulator->memWritePenalty(address));
        } else {
            value = uint16_t((uint16_t(emulator->readByte(address)) << 8) |
                              uint16_t(emulator->readByte(address + 1)));
            if (rnIsE) { regs.e[rn] = value; emulator->syncRegAfterUpperWordWrite(rn); }
            else       { regs.r[rn] = value; emulator->syncRegAfterWordWrite(rn); }
            emulator->addCycles(H8S_CYC_BASE_MEM);
            emulator->addCycles(emulator->memReadPenalty(address));
        }

        // MOV sets N and Z from the value moved and clears V; C is untouched.
        flags.zero     = (value == 0);
        flags.negative = (value & 0x8000) != 0;
        flags.overflow = false;

        if (!g_h8s_quiet_boot) {
            if (isStore) printf("[EXECUTE] 0x%06X: MOV.W R%d(=0x%04X), @(%d,ER%d) -> 0x%06X\n",
                                start, rn, value, int(disp), ern, address);
            else         printf("[EXECUTE] 0x%06X: MOV.W @(%d,ER%d)=0x%06X, R%d = 0x%04X\n",
                                start, int(disp), ern, address, rn, value);
        }
        return true;
    }

    // ==== Individual Instruction Implementations ====
    bool H8S2350InstructionExecutor::execute0x74Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register index from operand byte using 3-bit field (H8S standard)
        uint8_t ern = operand & 0x07;  // ER register index (0-7) - 3 bits

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in 0x74 instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get address from ER register
        uint32_t address = regs.er[ern];

        // Read long from memory (big-endian)
        uint32_t value = ((uint32_t)emulator->readByte(address) << 24) |
                          ((uint32_t)emulator->readByte(address + 1) << 16) |
                          ((uint32_t)emulator->readByte(address + 2) << 8) |
                          ((uint32_t)emulator->readByte(address + 3));

        // Store in VBR register (24-bit)
        regs.vbr = value & 0x00FFFFFF;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: LDC @ER%d, VBR (@0x%08X = 0x%08X, VBR=0x%06X)\n",
               pc - instruction.size, ern, address, value, regs.vbr & 0x00FFFFFF);

        return true;
    }

    // Handle 0x76 instruction (LDC ERn, VBR)
    bool H8S2350InstructionExecutor::execute0x76Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register index from operand byte using 3-bit field (H8S standard)
        uint8_t ern = operand & 0x07;  // ER register index (0-7) - 3 bits

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in 0x76 instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get value from ER register
        uint32_t value = regs.er[ern];

        // Store in VBR register (24-bit)
        regs.vbr = value & 0x00FFFFFF;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: LDC ER%d, VBR (0x%08X, VBR=0x%06X)\n",
               pc - instruction.size, ern, value, regs.vbr & 0x00FFFFFF);

        return true;
    }

    // Handle 0x77 instruction (BOR #imm3, @ERn)
    bool H8S2350InstructionExecutor::execute0x77Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t ern = operand & 0x07;              // ER register index (0-7)

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in 0x77 instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get address from ER register
        uint32_t address = regs.er[ern];

        // Read byte from memory
        uint8_t mem_value = emulator->readByte(address);

        // Perform OR operation
        uint8_t result = mem_value | (1 << immediate);

        // Write result back to memory
        emulator->writeByte(address, result);

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BOR #0x%X, @ER%d (@0x%08X: 0x%02X | 0x%02X = 0x%02X)\n",
               pc - instruction.size, immediate, ern, address, mem_value, (1 << immediate), result);

        return true;
    }

    // BUG52 - EEPMOV.B / EEPMOV.W, the block transfer instruction.
    //
    // Renesas H8S/2350 HM Rev 3.00, Appendix A, RENDERED PDF page 792 (printed "756"),
    // table "(8) Block Transfer Instructions":
    //
    //     EEPMOV.B   if R4L != 0 then  Repeat @ER5 -> @ER6, ER5+1 -> ER5,
    //                                         ER6+1 -> ER6, R4L-1 -> R4L
    //                                  Until R4L = 0
    //                else next;
    //     EEPMOV.W   the same with the SIXTEEN-bit counter R4
    //
    //     Condition Code:  I - H - N - Z - V - C -    NO flag is affected, not one.
    //     No. of States: 4 + 2n, n = the initial value of R4L or R4.
    //
    // The transfer unit is a BYTE in both forms - `@ER5 -> @ER6` with +1 on each pointer.
    // The .B/.W suffix names the width of the COUNTER, not of the datum, and the manual
    // states the block size in bytes for both (rendered page 85, printed "49").
    //
    // Modelled as an atomic loop. Section 5.5.4 (printed "117") says an interrupt during
    // an EEPMOV.W leaves the registers mid-transfer and re-executes from the instruction's
    // own address, which is exactly why the firmware wraps this one in
    //     0x01CD0: EEPMOV.W / 0x01CD4: OR.W R4,R4 / 0x01CD6: BNE -> 0x01CD0
    // - a resume loop. Running the transfer atomically leaves R4 = 0, so that BNE simply
    // falls through: the firmware's own guard makes the approximation invisible to it.
    // If interrupt latency during a 8 KB copy ever matters, this is the place to make it
    // re-entrant; it is recorded rather than silently assumed.
    bool H8S2350InstructionExecutor::execute0x7BInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        const uint32_t at = emulator->getProgramCounter() - instruction.size;

        // The encoding carries NO operand fields - the second byte is the only thing that
        // tells the two forms apart, and only 5C and D4 exist. Anything else is not an
        // EEPMOV, and inventing a behaviour for it is how this tree got its fictions.
        const uint8_t b2 = emulator->readByte(at + 1);
        const uint8_t b3 = emulator->readByte(at + 2);
        const uint8_t b4 = emulator->readByte(at + 3);
        if ((b2 != 0x5C && b2 != 0xD4) || b3 != 0x59 || b4 != 0x8F) {
            printf("[EEPMOV-UNKNOWN] 0x%06X: bytes %02X %02X %02X %02X - not an EEPMOV encoding "
                   "(RENDERED page 799: .B = 7B 5C 59 8F, .W = 7B D4 59 8F)\n",
                   at, 0x7B, b2, b3, b4);
            emulator->halt();
            return false;
        }

        const bool wide = (b2 == 0xD4);
        uint32_t count = wide ? (regs.er[4] & 0x0000FFFFu) : (regs.er[4] & 0x000000FFu);
        const uint32_t src0 = regs.er[5] & 0x00FFFFFFu;
        const uint32_t dst0 = regs.er[6] & 0x00FFFFFFu;

        uint32_t src = src0;
        uint32_t dst = dst0;
        uint32_t moved = 0;
        while (count != 0) {
            emulator->writeByte(dst, emulator->readByte(src));
            src = (src + 1) & 0x00FFFFFFu;
            dst = (dst + 1) & 0x00FFFFFFu;
            --count;
            ++moved;
        }

        // Write the pointers back as full longwords; the counter keeps the half of R4/ER4
        // the form owns and leaves the rest of ER4 alone, per the operation column.
        regs.er[5] = (regs.er[5] & 0xFF000000u) | src;
        regs.er[6] = (regs.er[6] & 0xFF000000u) | dst;
        if (wide) regs.er[4] &= 0xFFFF0000u;              // R4  -> 0
        else      regs.er[4] &= 0xFFFFFF00u;              // R4L -> 0
        emulator->syncRegAfterLongWrite(4);
        emulator->syncRegAfterLongWrite(5);
        emulator->syncRegAfterLongWrite(6);

        // NO condition code is affected - do not touch flags here.

        printf("[EEPMOV] 0x%06X: %s  %u bytes  0x%06X -> 0x%06X  (ER5=0x%06X ER6=0x%06X)\n",
               at, wide ? "EEPMOV.W" : "EEPMOV.B", moved, src0, dst0, src, dst);

        emulator->addCycles(4 + 2 * moved);
        return true;
    }

    // Handle 0x7C instruction (STC VBR, @ERn)
    bool H8S2350InstructionExecutor::execute0x7CInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register index from operand byte using 3-bit field (H8S standard)
        uint8_t ern = operand & 0x07;  // ER register index (0-7) - 3 bits

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in 0x7C instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get address from ER register
        uint32_t address = regs.er[ern];

        // Get VBR value (24-bit)
        uint32_t vbr_value = regs.vbr & 0x00FFFFFF;

        // Write VBR to memory (big-endian, 32-bit with high byte 0)
        emulator->writeByte(address, (vbr_value >> 16) & 0xFF);
        emulator->writeByte(address + 1, (vbr_value >> 8) & 0xFF);
        emulator->writeByte(address + 2, vbr_value & 0xFF);
        emulator->writeByte(address + 3, 0x00);  // High byte is 0 for 24-bit value

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: STC VBR, @ER%d (@0x%08X = 0x%06X)\n",
               pc - instruction.size, ern, address, vbr_value);

        return true;
    }

    // ===== LEGACY MIGRATION EXECUTORS =====
    // Migrated from legacy switch statement to new system

    // Handle 0x02 instruction (MOV.W #imm16, Rd)
    bool H8S2350InstructionExecutor::execute0x02Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the second byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit immediate value (big-endian)
        uint16_t immediate = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                            ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract register index from operand byte using 3-bit field (H8S standard)
        uint8_t rd = operand & 0x07;  // Destination register (0-7) - 3 bits

        // Validate register index
        if (rd >= 8) {
            printf("[ERROR] Invalid register index %d in 0x02 instruction at PC 0x%06X\n", rd, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Move immediate value to register (update only lower 16 bits)
        regs.r[rd] = (regs.r[rd] & 0xFFFF0000) | immediate;
        emulator->syncRegAfterWordWrite(rd);

        // Update flags based on the moved value
        flags.zero = (immediate == 0);
        flags.negative = (immediate & 0x8000) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.W #0x%04X, R%d\n", pc - instruction.size, immediate, rd);

        return true;
    }

    // Handle 0x03 instruction (MOV.L #imm32, ERd)
    bool H8S2350InstructionExecutor::execute0x03Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the second byte to get register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 32-bit immediate value (big-endian)
        uint32_t immediate = ((uint32_t)emulator->readByte(pc - instruction.size + 2) << 24) |
                            ((uint32_t)emulator->readByte(pc - instruction.size + 3) << 16) |
                            ((uint32_t)emulator->readByte(pc - instruction.size + 4) << 8) |
                            ((uint32_t)emulator->readByte(pc - instruction.size + 5));

        // Extract register index from operand byte using 3-bit field (H8S standard)
        uint8_t erd = operand & 0x07;  // ER destination register (0-7) - 3 bits

        // Validate register index
        if (erd >= 8) {
            printf("[ERROR] Invalid register index %d in 0x03 instruction at PC 0x%06X\n", erd, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Move immediate value to ER register
        regs.er[erd] = immediate;
        emulator->syncRegAfterLongWrite(erd);

        // Update flags based on the moved value
        flags.zero = (immediate == 0);
        flags.negative = (immediate & 0x80000000) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.L #0x%08X, ER%d\n", pc - instruction.size, immediate, erd);

        return true;
    }

    // Handle 0x04 instruction (MOV.B Rd, @(d:16, Rn))
    bool H8S2350InstructionExecutor::execute0x04Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the second byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract register indices from operand byte using 3-bit fields (H8S standard)
        uint8_t rd = (operand >> 4) & 0x07;  // Source register (0-7) - 3 bits
        uint8_t rn = operand & 0x07;         // Base register (0-7) - 3 bits

        // Validate register indices
        if (rd >= 8 || rn >= 8) {
            printf("[ERROR] Invalid register indices in 0x04 instruction: rd=%d rn=%d at PC 0x%06X\n", rd, rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get the value from source register
        uint8_t value = regs.r[rd] & 0xFF;

        // Calculate effective address: Rn + displacement
        uint32_t address = regs.r[rn] + displacement;

        // Write byte to memory
        emulator->writeByte(address, value);

        // Update flags based on the moved value
        flags.zero = (value == 0);
        flags.negative = (value & 0x80) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.B R%d(=0x%02X), @(0x%04X, R%d)(=0x%06X)\n", pc - instruction.size, rd, value, displacement, rn, address);

        return true;
    }

    // Handle 0x05 instruction (MOV.B @(d:16, Rn), Rd)
    bool H8S2350InstructionExecutor::execute0x05Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the second byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract register indices from operand byte using 3-bit fields (H8S standard)
        uint8_t rd = (operand >> 4) & 0x07;  // Destination register (0-7) - 3 bits
        uint8_t rn = operand & 0x07;         // Base register (0-7) - 3 bits

        // Validate register indices
        if (rd >= 8 || rn >= 8) {
            printf("[ERROR] Invalid register indices in 0x05 instruction: rd=%d rn=%d at PC 0x%06X\n", rd, rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Calculate effective address: Rn + displacement
        uint32_t address = regs.r[rn] + displacement;

        // Read byte from memory
        uint8_t value = emulator->readByte(address);

        // Write to destination register (update only lower 8 bits)
        regs.r[rd] = (regs.r[rd] & 0xFFFFFF00) | value;
        emulator->syncRegAfterWordWrite(rd);

        // Update flags based on the moved value
        flags.zero = (value == 0);
        flags.negative = (value & 0x80) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.B @(0x%04X, R%d)(=0x%06X)(=0x%02X), R%d\n", pc - instruction.size, displacement, rn, address, value, rd);

        return true;
    }

    // Handle 0x06 instruction (MOV.W Rd, @(d:16, Rn))
    bool H8S2350InstructionExecutor::execute0x06Instruction(const H8S2350Instruction& instruction,
                                                           class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the second byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract register indices from operand byte using 3-bit fields (H8S standard)
        uint8_t rd = (operand >> 4) & 0x07;  // Source register (0-7) - 3 bits
        uint8_t rn = operand & 0x07;         // Base register (0-7) - 3 bits

        // Validate register indices
        if (rd >= 8 || rn >= 8) {
            printf("[ERROR] Invalid register indices in 0x06 instruction: rd=%d rn=%d at PC 0x%06X\n", rd, rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get the value from source register
        uint16_t value = regs.r[rd] & 0xFFFF;

        // Calculate effective address: Rn + displacement
        uint32_t address = regs.r[rn] + displacement;

        // Write word to memory (big-endian)
        emulator->storeWord(address, value);   // BUG126: one bus cycle into CS0

        // Update flags based on the moved value
        flags.zero = (value == 0);
        flags.negative = (value & 0x8000) != 0;
        flags.carry = false;  // MOV doesn't affect carry
        flags.overflow = false;  // MOV doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[DECODE] 0x%06X: MOV.W R%d(=0x%04X), @(0x%04X, R%d)(=0x%06X)\n", pc - instruction.size, rd, value, displacement, rn, address);

        return true;
    }

    // ===== MEMORY OPERATIONS EXECUTOR IMPLEMENTATIONS =====

    // 0x80-0x8F = ADD.B #xx:8, Rd    (NOT "MOV.B #imm8, Rn" - the old name and the old
    // decode were both wrong; the function name is kept only because the header declares it).
    //
    // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED page 758:
    //     ADD.B #xx:8,Rd    8 rd | IMM        <- rd is the LOW NIBBLE OF THE FIRST BYTE,
    //                                            the SECOND byte is the WHOLE 8-bit immediate
    // (MOV.B #xx:8,Rd is the 0xFn row, handled by execute0xF9Instruction.)
    // Operation and flags, RENDERED page 738 (Arithmetic Instructions):
    //     Rd8 + #xx:8 -> Rd8      I -  H changes  N changes  Z changes  V changes  C changes
    // Byte register field, RENDERED page 771: 0-7 = R0H..R7H, 8-15 = R0L..R7L.
    //
    // 2026-09-13: this read the SECOND byte as if it held a 4-bit immediate plus a 4-bit
    // register, and then REJECTED register 8 - which is R0L and perfectly legal - and halted
    // the CPU. Measured at PC 0x006162 on `88 F8`, which is ADD.B #0xF8,R0L.
    // Same family as the inverted byte-register halves: a handler written before anyone had
    // read the register-field legend.
    bool H8S2350InstructionExecutor::executeMOV_B_IMM8_Rn(const H8S2350Instruction& instruction,
                                                         class H8S2350Emulator* emulator)
    {
        auto& regs  = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        const uint32_t start = emulator->getProgramCounter() - instruction.size;

        const uint8_t opbyte = emulator->readByte(start);        // 0x80..0x8F
        const uint8_t imm    = emulator->readByte(start + 1);    // the whole 8-bit immediate
        const uint8_t rfield = opbyte & 0x0F;                    // 0-15, ALL legal
        const uint8_t idx    = rfield & 0x07;
        const bool    high   = (rfield & 0x08) == 0;             // 0-7 = RnH

        const uint8_t dst    = high ? regs.rh[idx] : regs.rl[idx];
        const uint16_t wide  = uint16_t(dst) + uint16_t(imm);
        const uint8_t result = uint8_t(wide);

        if (high) regs.rh[idx] = result; else regs.rl[idx] = result;
        emulator->syncRegAfterByteWrite(idx, high);

        flags.zero       = (result == 0);
        flags.negative   = (result & 0x80u) != 0;
        flags.carry      = (wide > 0xFF);
        flags.half_carry = (((dst & 0x0F) + (imm & 0x0F)) > 0x0F);
        flags.overflow   = ((~(dst ^ imm) & (dst ^ result)) & 0x80u) != 0;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADD.B #0x%02X, R%d%c -> 0x%02X\n",
               start, imm, idx, high ? 'H' : 'L', result);
        return true;
    }

    // Handle 0xA0-0xA7 range (BRA @label)
    bool H8S2350InstructionExecutor::executeBRA_AT_LABEL(const H8S2350Instruction& instruction,
                                                        class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the 24-bit address (big-endian)
        uint32_t address = ((uint32_t)emulator->readByte(pc - instruction.size + 1) << 16) |
                           ((uint32_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                           ((uint32_t)emulator->readByte(pc - instruction.size + 3));

        // Branch to absolute address (24-bit)
        regs.pc = address & 0x00FFFFFF;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BRA @0x%06X\n", pc - instruction.size, address & 0x00FFFFFF);

        return true;
    }

    // Handle 0xA8-0xAF range (JSR @aa)
    bool H8S2350InstructionExecutor::executeJSR_AT_AA(const H8S2350Instruction& instruction,
                                                     class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t orig_pc = pc - instruction.size;  // PC at instruction start

        // Read the 16-bit address (big-endian)
        uint16_t address = ((uint16_t)emulator->readByte(pc - instruction.size + 1) << 8) |
                           ((uint16_t)emulator->readByte(pc - instruction.size + 2));

        // JSR @aa return address = PC after 3-byte instruction
        uint32_t expected_return = orig_pc + 3;
        uint32_t return_address = pc & 0x00FFFFFF;
        
        // Validate return address calculation
        if (return_address != expected_return) {
            printf("[JSR-VALIDATE] âš ď¸Ź  PC=0x%06X JSR @aa return mismatch: got=0x%06X expected=0x%06X\n",
                   orig_pc, return_address, expected_return);
        } else {
            printf("[JSR-VALIDATE] âś… PC=0x%06X JSR @aa return correct: 0x%06X\n", orig_pc, return_address);
        }

        // DEPRECATED: Direct ER7 manipulation - use push24 instead
        // regs.er[7] -= 4;
        emulator->syncRegAfterLongWrite(7);
        // emulator->writeLong(regs.er[7], pc & 0x00FFFFFF);
        
        // Use safe 24-bit push operation with alias synchronization
        emulator->push24(return_address);

        // Jump to subroutine
        regs.pc = address & 0x00FFFFFF;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: JSR @0x%04X (ret=0x%06X)\n",
               orig_pc, address, return_address);

        return true;
    }

    // Handle 0xE8 instruction (XOR.B Rn, Rm)
    bool H8S2350InstructionExecutor::executeAND_B_IMM8(const H8S2350Instruction& instruction,
                                                       class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        // AND.B #xx:8, Rd  =  0xEr IMM  (Renesas p.279/289)
        // low nibble r: 0-7 = RnH, 8-15 = RnL ; 2nd byte = immediate
        uint8_t opcodeByte = emulator->readByte(base);
        uint8_t imm8 = emulator->readByte(base + 1);
        uint8_t rnibble = opcodeByte & 0x0F;
        uint8_t regIdx = rnibble & 0x07;
        bool isHigh = (rnibble < 8);
        uint8_t cur = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
        uint8_t res = cur & imm8;
        if (isHigh) regs.rh[regIdx] = res; else regs.rl[regIdx] = res;
        emulator->syncRegAfterByteWrite(regIdx, isHigh);
        flags.zero = (res == 0);
        flags.negative = (res & 0x80) != 0;
        flags.overflow = false;  // AND clears V, leaves C
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: AND.B #0x%02X, R%d%c = 0x%02X\n",
               base, imm8, regIdx, isHigh ? 'H' : 'L', res);
        return true;
    }

    // OR.B #xx:8, Rd  =  0xCr IMM  (Renesas h8s2600 p.178). 2 bytes.
    bool H8S2350InstructionExecutor::executeOR_B_IMM8(const H8S2350Instruction& instruction,
                                                      class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t opcodeByte = emulator->readByte(base);
        uint8_t imm8 = emulator->readByte(base + 1);
        uint8_t rnibble = opcodeByte & 0x0F;
        uint8_t regIdx = rnibble & 0x07;
        bool isHigh = (rnibble < 8);
        uint8_t cur = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
        uint8_t res = cur | imm8;
        if (isHigh) regs.rh[regIdx] = res; else regs.rl[regIdx] = res;
        emulator->syncRegAfterByteWrite(regIdx, isHigh);
        flags.zero = (res == 0);
        flags.negative = (res & 0x80) != 0;
        flags.overflow = false;  // OR clears V, leaves C
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: OR.B #0x%02X, R%d%c = 0x%02X\n",
               base, imm8, regIdx, isHigh ? 'H' : 'L', res);
        return true;
    }

    // XOR.B #xx:8, Rd  =  0xDr IMM  (Renesas h8s2600 logical group). 2 bytes.
    bool H8S2350InstructionExecutor::executeXOR_B_IMM8(const H8S2350Instruction& instruction,
                                                       class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t opcodeByte = emulator->readByte(base);
        uint8_t imm8 = emulator->readByte(base + 1);
        uint8_t rnibble = opcodeByte & 0x0F;
        uint8_t regIdx = rnibble & 0x07;
        bool isHigh = (rnibble < 8);
        uint8_t cur = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
        uint8_t res = cur ^ imm8;
        if (isHigh) regs.rh[regIdx] = res; else regs.rl[regIdx] = res;
        emulator->syncRegAfterByteWrite(regIdx, isHigh);
        flags.zero = (res == 0);
        flags.negative = (res & 0x80) != 0;
        flags.overflow = false;  // XOR clears V, leaves C
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: XOR.B #0x%02X, R%d%c = 0x%02X\n",
               base, imm8, regIdx, isHigh ? 'H' : 'L', res);
        return true;
    }

    // CMP.B #xx:8, Rd  =  0xAr IMM  (Renesas p.107/299). 2 bytes. Compare only - sets flags, no writeback.
    //   low nibble r: 0-7 = RnH, 8-15 = RnL ; 2nd byte = 8-bit immediate.
    bool H8S2350InstructionExecutor::executeCMP_B_IMM8(const H8S2350Instruction& instruction,
                                                       class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t opcodeByte = emulator->readByte(base);
        uint8_t imm8 = emulator->readByte(base + 1);
        uint8_t rnibble = opcodeByte & 0x0F;
        uint8_t regIdx = rnibble & 0x07;
        bool isHigh = (rnibble < 8);
        uint8_t cur = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
        uint8_t res = (uint8_t)(cur - imm8);
        // CMP sets flags from (cur - imm8) but does NOT write back.
        flags.zero = (res == 0);
        flags.negative = (res & 0x80) != 0;
        flags.carry = (cur < imm8);                       // borrow
        flags.half_carry = (cur & 0x0F) < (imm8 & 0x0F);  // BUG121: H = borrow at bit 3 (REJ09B0139 RENDERED p.91 (CMP.B)); it was never written
        bool sc = (cur & 0x80) != 0, si = (imm8 & 0x80) != 0, sr = (res & 0x80) != 0;
        flags.overflow = (sc != si) && (sr != sc);        // signed overflow on subtract
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: CMP.B #0x%02X, R%d%c (R=0x%02X, Z=%d C=%d)\n",
               base, imm8, regIdx, isHigh ? 'H' : 'L', cur, flags.zero ? 1 : 0, flags.carry ? 1 : 0);
        return true;
    }

    // 0x0A = INC.B Rd (0A 0 rd) OR ADD.L ERs,ERd (0A 1ers0erd) - Renesas p.66/132/283
    bool H8S2350InstructionExecutor::execute0x0AInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t op2 = emulator->readByte(base + 1);
        if ((op2 & 0x80) != 0) {
            // ADD.L ERs, ERd  (0A 1ers0erd)
            uint8_t ers = (op2 >> 4) & 0x07;
            uint8_t erd = op2 & 0x07;
            uint32_t a = regs.er[erd], b = regs.er[ers];
            uint32_t res = a + b;
            emulator->setERd(erd, res);
            flags.zero = (res == 0);
            flags.negative = (res & 0x80000000u) != 0;
            flags.carry = (res < a);
            flags.half_carry = ((a & 0x0FFFFFFFu) + (b & 0x0FFFFFFFu)) > 0x0FFFFFFFu;   // BUG121: H = carry at bit 27 (REJ09B0139 RENDERED p.50)
            bool sa=(a&0x80000000u)!=0, sb=(b&0x80000000u)!=0, sr=(res&0x80000000u)!=0;
            flags.overflow = (sa==sb) && (sr!=sa);
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADD.L ER%d,ER%d = 0x%08X\n", base, ers, erd, res);
            return true;
        }
        // INC.B Rd  (0A 0 rd) : low nibble 0-7=RnH, 8-15=RnL
        uint8_t rnibble = op2 & 0x0F;
        uint8_t regIdx = rnibble & 0x07;
        bool isHigh = (rnibble < 8);
        uint8_t cur = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
        uint8_t res = (uint8_t)(cur + 1);
        if (isHigh) regs.rh[regIdx] = res; else regs.rl[regIdx] = res;
        emulator->syncRegAfterByteWrite(regIdx, isHigh);
        flags.zero = (res == 0);
        flags.negative = (res & 0x80) != 0;
        flags.overflow = (cur == 0x7F);   // signed overflow when 0x7F->0x80
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: INC.B R%d%c (0x%02X -> 0x%02X)\n",
               base, regIdx, isHigh ? 'H' : 'L', cur, res);
        return true;
    }

    // 0x0B = ADDS / INC.W / INC.L group (Renesas p.67/133/134/279)
    bool H8S2350InstructionExecutor::execute0x0BInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t op2 = emulator->readByte(base + 1);
        uint8_t hi  = (op2 >> 4) & 0x0F;
        uint8_t lo  = op2 & 0x0F;

        // ADDS #1/2/4, ERd : NO flag changes (Renesas: ADDS does not affect CCR)
        if (hi == 0x0 || hi == 0x8 || hi == 0x9) {
            uint8_t erd = lo & 0x07;
            uint32_t amt = (hi == 0x0) ? 1u : (hi == 0x8) ? 2u : 4u;
            uint32_t res = (regs.er[erd] + amt) & 0xFFFFFFFFu;
            emulator->setERd(erd, res);
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: ADDS #%u,ER%d = 0x%08X\n", base, amt, erd, res);
            return true;
        }
        // INC.W #1/#2, Rd : affects N,Z,V (not C)
        if (hi == 0x5 || hi == 0xD) {
            // BUG35, the mirror of DEC.W in execute0x1BInstruction: the 16-bit register
            // field is FOUR bits (RENDERED page 771, Appendix A.2 legend - 1000-1111 are
            // E0..E7). `lo & 0x07` silently incremented the wrong half.
            const uint8_t wreg = lo & 0x0F;
            uint16_t cur = (uint16_t)getRegisterValue(regs, wreg, 1);
            uint16_t amt = (hi == 0x5) ? 1 : 2;
            uint16_t res = (uint16_t)(cur + amt);
            setRegisterValue(regs, wreg, res, 1);
            if ((wreg & 0x07) == 7) emulator->syncRegAfterLongWrite(7);
            flags.zero = (res == 0);
            flags.negative = (res & 0x8000) != 0;
            // BUG121: V is SIGNED overflow, not the unsigned carry. REJ09B0139 RENDERED p.117
            // (INC.W) Notes: H'7FFF+1, H'7FFF+2 and H'7FFE+2 overflow - nothing else does.
            flags.overflow = ((~cur & res) & 0x8000u) != 0;
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: INC.W #%d,%s%d (0x%04X -> 0x%04X)\n",
                   base, amt, (wreg & 0x08) ? "E" : "R", wreg & 0x07, cur, res);
            return true;
        }
        // INC.L #1/#2, ERd : affects N,Z,V (not C)
        if (hi == 0x7 || hi == 0xF) {
            uint8_t erd = lo & 0x07;
            uint32_t cur = regs.er[erd];
            uint32_t amt = (hi == 0x7) ? 1u : 2u;
            uint32_t res = (cur + amt) & 0xFFFFFFFFu;
            emulator->setERd(erd, res);
            flags.zero = (res == 0);
            flags.negative = (res & 0x80000000u) != 0;
            // BUG121: signed overflow, REJ09B0139 RENDERED p.118 (INC.L) - H'7FFFFFFF+1/+2, H'7FFFFFFE+2.
            flags.overflow = ((~cur & res) & 0x80000000u) != 0;
            if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: INC.L #%u,ER%d (0x%08X -> 0x%08X)\n", base, amt, erd, cur, res);
            return true;
        }
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: 0x0B UNKNOWN subop hi=0x%X\n", base, hi);
        return true;
    }

    bool H8S2350InstructionExecutor::executeXOR_W_RN_RM(const H8S2350Instruction& instruction,
                                                       class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get register indices
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract register indices from operand byte
        uint8_t rn = (operand >> 4) & 0x0F;  // Destination register (0-7)
        uint8_t rm = operand & 0x0F;         // Source register (0-7)

        // Validate register indices
        if (rn >= 8 || rm >= 8) {
            printf("[ERROR] Invalid register indices in XOR.W Rn, Rm instruction: rn=%d rm=%d at PC 0x%06X\n", rn, rm, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register values
        uint16_t dst_value = regs.r[rn] & 0xFFFF;
        uint16_t src_value = regs.r[rm] & 0xFFFF;

        // Perform XOR operation
        uint16_t result = dst_value ^ src_value;

        // Store result in destination register
        regs.r[rn] = (regs.r[rn] & 0xFFFF0000) | result;
        emulator->syncRegAfterWordWrite(rn);

        // Update flags
        flags.zero = (result == 0);
        flags.negative = (result & 0x8000) != 0;
        flags.carry = false;  // XOR doesn't affect carry
        flags.overflow = false;  // XOR doesn't affect overflow

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: XOR.W R%d, R%d (0x%04X ^ 0x%04X = 0x%04X, Z=%d N=%d)\n",
               pc - instruction.size, rn, rm, dst_value, src_value, result, flags.zero, flags.negative);

        return true;
    }

    // Handle 0xF4 instruction (BCLR #imm3, @(disp, ERn))
    bool H8S2350InstructionExecutor::executeBCLR_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                                 class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t ern = operand & 0x07;              // ER register index (0-7)

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in BCLR instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Calculate effective address: ERn + displacement
        uint32_t address = regs.er[ern] + displacement;

        // Read byte from memory
        uint8_t mem_value = emulator->readByte(address);

        // Clear the specified bit
        uint8_t result = mem_value & ~(1 << immediate);

        // Write result back to memory
        emulator->writeByte(address, result);

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BCLR #0x%X, @(0x%04X, ER%d) (@0x%08X: 0x%02X & ~0x%02X = 0x%02X)\n",
               pc - instruction.size, immediate, displacement, ern, address, mem_value, (1 << immediate), result);

        return true;
    }

    // 0x72 = BCLR #xx:3, Rd (register-direct bit clear) - Renesas p.79/280.
    bool H8S2350InstructionExecutor::executeBCLR_IMM3_REG(const H8S2350Instruction& instruction,
                                                          class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t base = pc - instruction.size;
        uint8_t op2 = emulator->readByte(base + 1);
        uint8_t bit = (op2 >> 4) & 0x07;          // bit number 0-7
        uint8_t rnibble = op2 & 0x0F;             // 0-7=RnH, 8-15=RnL
        uint8_t regIdx = rnibble & 0x07;
        bool isHigh = (rnibble < 8);
        uint8_t cur = isHigh ? regs.rh[regIdx] : regs.rl[regIdx];
        uint8_t res = (uint8_t)(cur & ~(1u << bit));   // clear the bit; CCR unaffected
        if (isHigh) regs.rh[regIdx] = res; else regs.rl[regIdx] = res;
        emulator->syncRegAfterByteWrite(regIdx, isHigh);
        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BCLR #%d,R%d%c (0x%02X -> 0x%02X)\n",
               base, bit, regIdx, isHigh ? 'H' : 'L', cur, res);
        return true;
    }

    // Handle 0xF6 instruction (BAND #imm3, @(disp, ERn))
    bool H8S2350InstructionExecutor::executeBAND_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                                 class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t ern = operand & 0x07;              // ER register index (0-7)

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in BAND instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Calculate effective address: ERn + displacement
        uint32_t address = regs.er[ern] + displacement;

        // Read byte from memory
        uint8_t mem_value = emulator->readByte(address);

        // Perform AND operation with bit mask
        uint8_t result = mem_value & (1 << immediate);

        // Write result back to memory
        emulator->writeByte(address, result);

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BAND #0x%X, @(0x%04X, ER%d) (@0x%08X: 0x%02X & 0x%02X = 0x%02X)\n",
               pc - instruction.size, immediate, displacement, ern, address, mem_value, (1 << immediate), result);

        return true;
    }

    // Handle 0xF7 instruction (BOR #imm3, @(disp, ERn))
    bool H8S2350InstructionExecutor::executeBOR_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                                class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t ern = operand & 0x07;              // ER register index (0-7)

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in BOR instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Calculate effective address: ERn + displacement
        uint32_t address = regs.er[ern] + displacement;

        // Read byte from memory
        uint8_t mem_value = emulator->readByte(address);

        // Perform OR operation with bit mask
        uint8_t result = mem_value | (1 << immediate);

        // Write result back to memory
        emulator->writeByte(address, result);

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BOR #0x%X, @(0x%04X, ER%d) (@0x%08X: 0x%02X | 0x%02X = 0x%02X)\n",
               pc - instruction.size, immediate, displacement, ern, address, mem_value, (1 << immediate), result);

        return true;
    }

    // Handle 0xF8 instruction (BXOR #imm3, @(disp, ERn))
    bool H8S2350InstructionExecutor::executeBXOR_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                                 class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Read the 16-bit displacement (big-endian)
        uint16_t displacement = ((uint16_t)emulator->readByte(pc - instruction.size + 2) << 8) |
                               ((uint16_t)emulator->readByte(pc - instruction.size + 3));

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t ern = operand & 0x07;              // ER register index (0-7)

        // Validate register index
        if (ern >= 8) {
            printf("[ERROR] Invalid register index %d in BXOR instruction at PC 0x%06X\n", ern, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Calculate effective address: ERn + displacement
        uint32_t address = regs.er[ern] + displacement;

        // Read byte from memory
        uint8_t mem_value = emulator->readByte(address);

        // Perform XOR operation with bit mask
        uint8_t result = mem_value ^ (1 << immediate);

        // Write result back to memory
        emulator->writeByte(address, result);

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BXOR #0x%X, @(0x%04X, ER%d) (@0x%08X: 0x%02X ^ 0x%02X = 0x%02X)\n",
               pc - instruction.size, immediate, displacement, ern, address, mem_value, (1 << immediate), result);

        return true;
    }

    // Handle 0xFC instruction (BTST #imm3, Rn)
    bool H8S2350InstructionExecutor::executeBTST_IMM3_RN(const H8S2350Instruction& instruction,
                                                        class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t rn = operand & 0x07;               // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in BTST instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Test the specified bit
        bool bit_set = (reg_value & (1 << immediate)) != 0;

        // Update zero flag (1 if bit is clear, 0 if bit is set)
        flags.zero = !bit_set;
        flags.carry = false;  // BTST doesn't affect carry
        flags.overflow = false;  // BTST doesn't affect overflow
        flags.negative = false;  // BTST doesn't affect negative

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BTST #0x%X, R%d (0x%02X bit %d = %d, Z=%d)\n",
               pc - instruction.size, immediate, rn, reg_value, immediate, bit_set, flags.zero);

        return true;
    }

    // Handle 0xFE instruction (BXOR #imm3, Rn)
    bool H8S2350InstructionExecutor::executeBXOR_IMM3_RN(const H8S2350Instruction& instruction,
                                                        class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();

        // Read the operand byte to get immediate value and register index
        uint8_t operand = emulator->readByte(pc - instruction.size + 1);

        // Extract immediate value (bits 6-4) and register index (bits 2-0)
        uint8_t immediate = (operand >> 4) & 0x07;  // 3-bit immediate (0-7)
        uint8_t rn = operand & 0x07;               // Register index (0-7)

        // Validate register index
        if (rn >= 8) {
            printf("[ERROR] Invalid register index %d in BXOR instruction at PC 0x%06X\n", rn, pc - instruction.size);
            emulator->handleIllegalInstruction();
            return true;
        }

        // Get register value
        uint8_t reg_value = regs.r[rn] & 0xFF;

        // Perform XOR operation with bit mask
        uint8_t result = reg_value ^ (1 << immediate);

        // Store result in register
        regs.rl[rn] = result;  // FIX: write RnL directly (was r[rn] word-write)
        emulator->syncRegAfterByteWrite(rn, false);  // FIX: byte-sync preserves ER7/SP upper bits

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: BXOR #0x%X, R%d (0x%02X ^ 0x%02X = 0x%02X)\n",
               pc - instruction.size, immediate, rn, reg_value, (1 << immediate), result);

        return true;
    }

    // Handle 0x7E instruction (BLE - Branch if Less or Equal, disp8)
    bool H8S2350InstructionExecutor::execute0x7EInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the displacement byte (signed 8-bit)
        int8_t displacement = (int8_t)emulator->readByte(pc - instruction.size + 1);

        // BLE condition: Z==1 || N!=V (Branch if Less or Equal)
        bool condition = flags.zero || (flags.negative != flags.overflow);
        
        if (condition) {
            uint32_t target = (pc + displacement) & 0x00FFFFFF;
            printf("[BRANCH] 0x7E: BLE taken, PC=0x%06X + disp=%d -> 0x%06X (Z=%d N=%d V=%d)\n", 
                   pc - instruction.size, displacement, target, flags.zero, flags.negative, flags.overflow);
            regs.pc = target;
        } else {
            printf("[BRANCH] 0x7E: BLE not taken, PC=0x%06X + %d (Z=%d N=%d V=%d)\n", 
                   pc - instruction.size, displacement, flags.zero, flags.negative, flags.overflow);
        }

        return true;
    }

    // Handle 0x7D instruction (BGT - Branch if Greater Than, disp8)
    bool H8S2350InstructionExecutor::execute0x7DInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();

        // Read the displacement byte (signed 8-bit)
        int8_t displacement = (int8_t)emulator->readByte(pc - instruction.size + 1);

        // BGT condition: Z==0 && N==V (Branch if Greater Than)
        bool condition = !flags.zero && (flags.negative == flags.overflow);
        
        if (condition) {
            uint32_t target = (pc + displacement) & 0x00FFFFFF;
            printf("[BRANCH] 0x7D: BGT taken, PC=0x%06X + disp=%d -> 0x%06X (Z=%d N=%d V=%d)\n", 
                   pc - instruction.size, displacement, target, flags.zero, flags.negative, flags.overflow);
            regs.pc = target;
        } else {
            printf("[BRANCH] 0x7D: BGT not taken, PC=0x%06X + %d (Z=%d N=%d V=%d)\n", 
                   pc - instruction.size, displacement, flags.zero, flags.negative, flags.overflow);
        }

        return true;
    }

    // BUG39, 2026-09-13 - 0x5D IS `JSR @ERn`, NOT `BSR d:16`.
    //
    // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED PDF page 800
    // (printed "page 764 of 988"), the whole jump/call block in one place:
    //
    //     JMP @ERn     -   5 9 | 0:ern 0        2 bytes
    //     JMP @aa:24   -   5 A | abs24          4 bytes
    //     JMP @@aa:8   -   5 B | abs            2 bytes
    //     JSR @ERn     -   5 D | 0:ern 0        2 bytes
    //     JSR @aa:24   -   5 E | abs24          4 bytes
    //     JSR @@aa:8   -   5 F | abs            2 bytes
    //
    // `BSR d:16` is `5C 00 | disp` - FOUR bytes, a different primary - and the
    // firmware uses it at 0x01FD66 (`5C 00 00 B2`), handled elsewhere.
    //
    // What stood here read two bytes of "displacement" out of the instruction
    // stream, computed PC + disp and jumped there. Measured on the live path: at
    // 0x002710 the bytes are `5D 10 6A` = JSR @ER1, and this handler printed its
    // own complaint - "[BSR-VALIDATE] PC=0x002710 BSR d:16 return mismatch:
    // got=0x002712 expected=0x002713" - then jumped to 0x00377C, nowhere near
    // ER1. Twelve instructions later SP was 4 bytes above its own base and the
    // machine returned to 0x000000.
    //
    // A HANDLER THAT PRINTS A MISMATCH AND CARRIES ON ANYWAY IS NOT A CHECK. That
    // validate line had been firing every time this opcode was reached, and it was
    // reporting the fiction to itself.
    bool H8S2350InstructionExecutor::execute0x5DInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        const uint32_t pc_after = emulator->getProgramCounter();   // already past the 2 bytes
        const uint32_t at       = pc_after - instruction.size;
        const uint8_t  op2      = emulator->readByte(at + 1);
        const uint8_t  ern      = (op2 >> 4) & 0x07;               // `0:ern` - bit 7 is the 0

        const uint32_t return_address = pc_after & 0x00FFFFFF;
        const uint32_t target         = regs.er[ern] & 0x00FFFFFF;

        emulator->push24(return_address);
        regs.pc = target;

        if (!g_h8s_quiet_boot) printf("[EXECUTE] 0x%06X: JSR @ER%d -> 0x%06X (return 0x%06X)\n",
               at, ern, target, return_address);
        emulator->addCycles(6);
        return true;
    }

    // The old BSR-d:16-on-0x5D body, kept compiled out so the shape stays on the
    // record next to the correction above.
#if 0
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t orig_pc = pc - instruction.size;  // PC at instruction start

        // Read the 16-bit displacement (big-endian, signed)
        uint8_t high_byte = emulator->readByte(pc - instruction.size + 1);
        uint8_t low_byte = emulator->readByte(pc - instruction.size + 2);
        int16_t displacement = (int16_t)((high_byte << 8) | low_byte);
        
        // BSR d:16 return address = PC after 3-byte instruction
        uint32_t expected_return = orig_pc + 3;
        uint32_t return_address = pc & 0x00FFFFFF;  // Current PC is already after the instruction
        
        // Validate return address calculation
        if (return_address != expected_return) {
            printf("[BSR-VALIDATE] âš ď¸Ź  PC=0x%06X BSR d:16 return mismatch: got=0x%06X expected=0x%06X\n",
                   orig_pc, return_address, expected_return);
        } else {
            printf("[BSR-VALIDATE] âś… PC=0x%06X BSR d:16 return correct: 0x%06X\n", orig_pc, return_address);
        }
        
        // Calculate target address (PC + displacement)
        uint32_t target = (pc + displacement) & 0x00FFFFFF;
        
        // Push return address onto stack using 24-bit operation (advanced mode)
        printf("[BSR] 0x5D: Pushing return address 0x%06X, jumping to 0x%06X (disp=%d)\n", 
               return_address, target, displacement);
        emulator->push24(return_address);

        // Set PC to target address
        regs.pc = target;

        return true;
    }
#endif

    // Handle 0x5B instruction (BSR d:8 - Branch to Subroutine, 8-bit displacement)
    bool H8S2350InstructionExecutor::execute0x5BInstruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        uint32_t pc = emulator->getProgramCounter();
        uint32_t orig_pc = pc - instruction.size;  // PC at instruction start

        // Read the displacement byte (signed 8-bit)
        int8_t displacement = (int8_t)emulator->readByte(pc - instruction.size + 1);
        
        // BSR d:8 return address = PC after 2-byte instruction
        uint32_t expected_return = orig_pc + 2;
        uint32_t return_address = pc & 0x00FFFFFF;  // Current PC is already after the instruction
        
        // Validate return address calculation
        if (return_address != expected_return) {
            printf("[BSR-VALIDATE] âš ď¸Ź  PC=0x%06X BSR d:8 return mismatch: got=0x%06X expected=0x%06X\n",
                   orig_pc, return_address, expected_return);
        } else {
            printf("[BSR-VALIDATE] âś… PC=0x%06X BSR d:8 return correct: 0x%06X\n", orig_pc, return_address);
        }
        
        // Calculate target address (PC + displacement)
        uint32_t target = (pc + displacement) & 0x00FFFFFF;
        
        // Push return address onto stack using 24-bit operation (advanced mode)
        printf("[BSR] 0x5B: Pushing return address 0x%06X, jumping to 0x%06X (disp=%d)\n", 
               return_address, target, displacement);
        emulator->push24(return_address);
        
        // Set PC to target address
        regs.pc = target;
        
        return true;
    }

    // Handle 0x57 instruction (TRAPA #imm8 - Trap Always) - H8S/2350 manual compliant
    bool H8S2350InstructionExecutor::execute0x57Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        // Use the 2-bit vector number decoded from instruction (0-3)
        uint8_t trap_number = instruction.immediate_value & 0x03;
        
        printf("[TRAPA-EXEC] TRAPA #%d (0x57 0x%02X)\n", trap_number, 
               trap_number << 2);  // Show the second byte pattern
        
        // Delegate to emulator's manual-compliant implementation
        return emulator->executeTrap(trap_number);
    }

    // RTE functionality moved to different opcode - 0x56 is PUSH Rn

    // Handle 0x00 instruction (NOP - No Operation)
    bool H8S2350InstructionExecutor::execute0x00Instruction(const H8S2350Instruction& instruction,
                                                            class H8S2350Emulator* emulator)
    {
        // NOP: No operation - just consume cycles
        printf("[NOP] 0x00: No operation, cycles consumed\n");
        return true;
    }
    
    // Handle 0x00 prefix Extended ALU operations (0x80-9F)
    bool H8S2350InstructionExecutor::executeExtendedALU(const H8S2350Instruction& instruction,
                                                        H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        uint32_t pc = emulator->getProgramCounter();
        
        // Extract operation details from 0x00 8x/9x instruction
        uint8_t extended_op = instruction.opcode & 0xFF;
        
        // 0x80-9F: IMM8 â†’ Rn operations (length=3: 0x00 + op1 + imm8)
        if (extended_op >= 0x80 && extended_op <= 0x9F) {
            uint8_t rn_idx = extended_op & 0x07;  // Register index (R0-R7)
            uint8_t op_family = (extended_op >> 3) & 0x03;  // 0x80-87, 0x88-8F, 0x90-97, 0x98-9F
            uint8_t imm8 = emulator->readByte(pc - instruction.size + 2);  // Immediate value
            
            // H8S/2350 8-bit register access helpers (RnL = lower byte)
            auto getRnL = [&](int n) -> uint8_t {
                return static_cast<uint8_t>(regs.r[n] & 0x00FF);
            };
            auto setRnL = [&](int n, uint8_t v) {
                regs.r[n] = static_cast<uint16_t>((regs.r[n] & 0xFF00) | v);
                emulator->syncRegAfterWordWrite(n);
            };
            
            // CCR helper functions for logical operations
            auto setZN = [&](uint8_t result) {
                flags.zero = (result == 0);
                flags.negative = (result & 0x80) != 0;
            };
            auto clearCV = [&]() {
                flags.carry = false;
                flags.overflow = false;
            };
            
            // H8S 8-bit flag helpers (ADD/CMP) - H8S/2350 compatible
            auto setZN_8 = [&](uint8_t r) {
                flags.zero = (r == 0);
                flags.negative = (r & 0x80) != 0;
            };
            
            // ADD (r = a + b) â€” H8S convention
            auto flags_add8 = [&](uint8_t a, uint8_t b, uint8_t r) {
                // Carry (C): carry out from bit7
                flags.carry = ((uint16_t)a + (uint16_t)b) > 0xFF;
                // Half-carry (H): carry from bit3->bit4
                flags.half_carry = ((a & 0x0F) + (b & 0x0F)) > 0x0F;
                // Overflow (V): same sign operands â†’ different sign result
                flags.overflow = ((~(a ^ b) & (a ^ r)) & 0x80) != 0;
                setZN_8(r);
            };
            
            // SUB/CMP (r = a - b) â€” H8S convention
            auto flags_sub8 = [&](uint8_t a, uint8_t b, uint8_t r) {
                // Carry (C): H8S SUB/CMP "no borrow" = 1, borrow = 0
                flags.carry = a >= b;
                // Half-carry (H): "no borrow from bit3" = 1
                flags.half_carry = ((a & 0x0F) >= (b & 0x0F));
                // Overflow (V): different sign subtraction, result sign differs from a
                flags.overflow = (((a ^ b) & (a ^ r)) & 0x80) != 0;
                setZN_8(r);
            };
            
            // DAA/DAS BCD correction helpers - H8S/2350 compatible
            auto daa8_apply = [&](uint8_t a, uint8_t& out) {
                uint8_t corr = 0;
                const bool C_in = flags.carry;
                const bool H_in = flags.half_carry;

                if (((a & 0x0F) > 0x09) || H_in) corr |= 0x06;
                if ((a > 0x99) || C_in)        corr |= 0x60;

                const uint16_t res = (uint16_t)a + corr;
                out = (uint8_t)res;

                // Update flags after DAA
                flags.carry       = (res > 0xFF);                                   // new C
                flags.half_carry  = (((a & 0x0F) + (corr & 0x0F)) > 0x0F);        // new H
                flags.overflow    = false;                                          // V=0
                flags.zero        = (out == 0);
                flags.negative    = (out & 0x80) != 0;
            };

            auto das8_apply = [&](uint8_t a, uint8_t& out) {
                uint8_t corr = 0;
                const bool borrow_in     = !flags.carry;        // C=1 â†’ no-borrow
                const bool h_borrow_in   = !flags.half_carry;   // H=1 â†’ no-borrow (lower nibble)

                if (((a & 0x0F) > 0x09) || h_borrow_in) corr |= 0x06;
                if ((a > 0x99) || borrow_in)           corr |= 0x60;

                // Corrected subtraction
                const int16_t res16 = (int16_t)a - (int16_t)corr;
                out = (uint8_t)res16;

                // Update flags after DAS (H8S: C=1 â†’ no-borrow)
                flags.carry       = ((uint8_t)a >= corr);                               // no-borrow?
                flags.half_carry  = ((a & 0x0F) >= (corr & 0x0F));                      // nibble no-borrow
                flags.overflow    = false;                                              // V=0
                flags.zero        = (out == 0);
                flags.negative    = (out & 0x80) != 0;
            };
            
            // Get current RnL value and perform operation
            uint8_t old_rn = getRnL(rn_idx);
            uint8_t new_rn;
            
            switch (op_family) {
                case 0: {  // 0x80-87: AND #imm8, RnL
                    new_rn = old_rn & imm8;
                    setRnL(rn_idx, new_rn);
                    setZN(new_rn);
                    clearCV();
                    printf("[EXT-ALU] AND #0x%02X, R%dL: 0x%02X & 0x%02X = 0x%02X | CCR(ZNVC)=%d%d%d%d\n", 
                           imm8, rn_idx, old_rn, imm8, new_rn, flags.zero, flags.negative, flags.overflow, flags.carry);
                    break;
                }
                case 1: {  // 0x88-8F: XOR #imm8, RnL
                    new_rn = old_rn ^ imm8;
                    setRnL(rn_idx, new_rn);
                    setZN(new_rn);
                    clearCV();
                    printf("[EXT-ALU] XOR #0x%02X, R%dL: 0x%02X ^ 0x%02X = 0x%02X | CCR(ZNVC)=%d%d%d%d\n", 
                           imm8, rn_idx, old_rn, imm8, new_rn, flags.zero, flags.negative, flags.overflow, flags.carry);
                    break;
                }
                case 2: {  // 0x90-97: OR #imm8, RnL
                    new_rn = old_rn | imm8;
                    setRnL(rn_idx, new_rn);
                    setZN(new_rn);
                    clearCV();
                    printf("[EXT-ALU] OR #0x%02X, R%dL: 0x%02X | 0x%02X = 0x%02X | CCR(ZNVC)=%d%d%d%d\n", 
                           imm8, rn_idx, old_rn, imm8, new_rn, flags.zero, flags.negative, flags.overflow, flags.carry);
                    break;
                }
                case 3: {  // 0x98-9F: ADD/CMP #imm8, RnL
                    if (rn_idx < 4) {  // 0x98-9B: ADD #imm8, RnL
                        // Perform 8-bit addition with H8S-compatible flag calculation
                        uint8_t a = old_rn, b = imm8, r = static_cast<uint8_t>(a + b);
                        
                        setRnL(rn_idx, r);
                        flags_add8(a, b, r);
                        
                        printf("[EXT-ALU] ADD #0x%02X, R%dL: 0x%02X + 0x%02X = 0x%02X | CCR(ZNHVC)=%d%d%d%d%d\n", 
                               imm8, rn_idx, a, b, r, flags.zero, flags.negative, 
                               flags.half_carry, flags.overflow, flags.carry);
                    } else {  // 0x9C-9F: CMP #imm8, RnL
                        // Perform 8-bit subtraction (CMP = SUB without storing result)
                        uint8_t a = old_rn, b = imm8, r = static_cast<uint8_t>(a - b);
                        
                        flags_sub8(a, b, r);
                        
                        // CMP does not modify destination register
                        printf("[EXT-ALU] CMP #0x%02X, R%dL: 0x%02X - 0x%02X = 0x%02X | CCR(ZNHVC)=%d%d%d%d%d (no store)\n", 
                               imm8, rn_idx, a, b, r, flags.zero, flags.negative, 
                               flags.half_carry, flags.overflow, flags.carry);
                    }
                    break;
                }
                default: {
                    printf("[EXT-ALU] Unknown op_family: 0x%02X\n", op_family);
                    break;
                }
            }
            
            return true;
        }
        
        // Unknown extended ALU operation
        printf("[EXT-ALU] Unknown operation: 0x%02X\n", extended_op);
        return true;
    }

    // DAA/DAS BCD correction executors - H8S/2350 compatible
    bool H8S2350InstructionExecutor::executeDAA(const H8S2350Instruction& instruction,
                                                 H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        // Extract register index (typically from low 3 bits of opcode)
        uint8_t rn_idx = instruction.opcode & 0x07;
        
        // RnL access helpers
        auto getRnL = [&](int n) -> uint8_t {
            return static_cast<uint8_t>(regs.r[n] & 0x00FF);
        };
        auto setRnL = [&](int n, uint8_t v) {
            regs.r[n] = static_cast<uint16_t>((regs.r[n] & 0xFF00) | v);
            emulator->syncRegAfterWordWrite(n);
        };
        
        uint8_t a = getRnL(rn_idx);
        uint8_t result;
        
        // DAA BCD correction logic
        uint8_t corr = 0;
        const bool C_in = flags.carry;
        const bool H_in = flags.half_carry;

        if (((a & 0x0F) > 0x09) || H_in) corr |= 0x06;
        if ((a > 0x99) || C_in)        corr |= 0x60;

        const uint16_t res = (uint16_t)a + corr;
        result = (uint8_t)res;

        // Update flags after DAA
        flags.carry       = (res > 0xFF);                                   
        flags.half_carry  = (((a & 0x0F) + (corr & 0x0F)) > 0x0F);        
        flags.overflow    = false;                                          // V=0 for BCD
        flags.zero        = (result == 0);
        flags.negative    = (result & 0x80) != 0;
        
        setRnL(rn_idx, result);
        
        printf("[BCD] DAA R%dL: 0x%02X + corr(0x%02X) = 0x%02X | CCR(ZNHVC)=%d%d%d%d%d (2 cycles)\n", 
               rn_idx, a, corr, result, flags.zero, flags.negative, 
               flags.half_carry, flags.overflow, flags.carry);
        
        // H8S/2350 DAA timing: 2 machine cycles (TODO: implement addCycles method)
        return true;
    }

    bool H8S2350InstructionExecutor::executeDAS(const H8S2350Instruction& instruction,
                                                 H8S2350Emulator* emulator)
    {
        auto& regs = emulator->getRegisters();
        auto& flags = emulator->getFlags();
        
        // Extract register index (typically from low 3 bits of opcode)
        uint8_t rn_idx = instruction.opcode & 0x07;
        
        // RnL access helpers
        auto getRnL = [&](int n) -> uint8_t {
            return static_cast<uint8_t>(regs.r[n] & 0x00FF);
        };
        auto setRnL = [&](int n, uint8_t v) {
            regs.r[n] = static_cast<uint16_t>((regs.r[n] & 0xFF00) | v);
            emulator->syncRegAfterWordWrite(n);
        };
        
        uint8_t a = getRnL(rn_idx);
        uint8_t result;
        
        // DAS BCD correction logic
        uint8_t corr = 0;
        const bool borrow_in     = !flags.carry;        // C=1 â†’ no-borrow
        const bool h_borrow_in   = !flags.half_carry;   // H=1 â†’ no-borrow (lower nibble)

        if (((a & 0x0F) > 0x09) || h_borrow_in) corr |= 0x06;
        if ((a > 0x99) || borrow_in)           corr |= 0x60;

        // Corrected subtraction
        const int16_t res16 = (int16_t)a - (int16_t)corr;
        result = (uint8_t)res16;

        // Update flags after DAS (H8S: C=1 â†’ no-borrow)
        flags.carry       = ((uint8_t)a >= corr);                               
        flags.half_carry  = ((a & 0x0F) >= (corr & 0x0F));                      
        flags.overflow    = false;                                              // V=0 for BCD
        flags.zero        = (result == 0);
        flags.negative    = (result & 0x80) != 0;
        
        setRnL(rn_idx, result);
        
        printf("[BCD] DAS R%dL: 0x%02X - corr(0x%02X) = 0x%02X | CCR(ZNHVC)=%d%d%d%d%d (2 cycles)\n", 
               rn_idx, a, corr, result, flags.zero, flags.negative, 
               flags.half_carry, flags.overflow, flags.carry);
        
        // H8S/2350 DAS timing: 2 machine cycles (TODO: implement addCycles method)
        return true;
    }

}
