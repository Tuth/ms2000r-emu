#pragma once

#include <cstdint>
#include <string>

namespace MS2000
{
    // H8S2350 Instruction Set Definitions
    
    // Opcode ranges for different instruction types
    namespace H8S2350Opcode
    {
        // Basic instruction opcodes (high nibble)
        constexpr uint8_t MOV_B = 0x7F;    // Move byte (moved to avoid 0x00 NOP collision)
        constexpr uint8_t MOV_W = 0x01;    // Move word
        constexpr uint8_t MOV_L = 0x02;    // Move long
        // 2026-09-13, BUG38. THESE FOUR WERE FICTION AND ARE RETIRED.
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1: 0x03-0x07 is the CCR group -
        //   0x03 LDC Rs,CCR / Rs,EXR   (RENDERED PDF page 800, printed "764")
        //   0x04 ORC  #xx:8,CCR        (RENDERED PDF page 803, printed "767")
        //   0x05 XORC #xx:8,CCR        (RENDERED PDF page 807, printed "771")
        //   0x06 ANDC #xx:8,CCR        (RENDERED PDF page 794, printed "758")
        //   0x07 LDC  #xx:8,CCR        (RENDERED PDF page 800)
        // The real ADD.B/ADD.W are 0x08/0x09 and ADD.L ERs,ERd is `0A 1:ers 0:erd`.
        //   constexpr uint8_t ADD_B = 0x03;   RETIRED - is LDC Rs,CCR/EXR
        //   constexpr uint8_t ADD_W = 0x04;   RETIRED - is ORC  #xx:8,CCR
        //   constexpr uint8_t ADD_L = 0x05;   RETIRED - is XORC #xx:8,CCR
        //   constexpr uint8_t SUB_B = 0x06;   RETIRED - is ANDC #xx:8,CCR
        //
        // THE WHOLE ENUM ABOVE AND BELOW IS TIER 3 AND ITS OWN COMMENTS SAY SO:
        // "moved to avoid 0x00 NOP collision", "moved to avoid 0x07 instruction
        // collision". These values were CHOSEN TO FIT A TABLE, not read from the
        // manual. Four have already been struck as wrong in the executor (CMP_W,
        // CMP_L, AND_B, AND_W), three more with BUG37 (EXT_W, EXTU_B, EXTU_W), and
        // four here. Do not add a handler keyed on any of them without rendering
        // the page first. AUDIT-QUEUE: MOV_B=0x7F, MOV_W=0x01 (0x01 is ALWAYS a
        // prefix), MOV_L=0x02, SUB_W=0x80, SUB_L=0x08.
        constexpr uint8_t SUB_W = 0x80;    // Subtract word (moved to avoid 0x07 instruction collision)
        constexpr uint8_t SUB_L = 0x08;    // Subtract long
        
        // P1.6 SUBX (Subtract with extend) - H8S/2600 Manual exact format
        constexpr uint8_t SUBX_REG = 0x1E;      // SUBX Rs,Rd : 1E, (rs<<4)|rd  
        constexpr uint8_t SUBX_IMM_BASE = 0xB0; // SUBX #imm8,Rd : (B0|rd), imm8
        constexpr uint8_t CMP_B = 0x09;    // Compare byte
        constexpr uint8_t CMP_W = 0x0A;    // Compare word
        constexpr uint8_t CMP_L = 0x0B;    // Compare long
        constexpr uint8_t AND_B = 0x0C;    // AND byte
        constexpr uint8_t AND_W = 0x0D;    // AND word
        constexpr uint8_t AND_L = 0x0E;    // AND long
        // 2026-09-17, BUG84 - RETIRED: OR_B = 0x0F.
        // 0x0F is `DAA Rd` (bit 7 clear) and `MOV.L ERs,ERd` (bit 7 set) - RENDERED
        // PDF pages 799 (printed "763") and 802 (printed "766"). The real OR.B is
        // `1 4 | rs rd`, which BUG51 read off RENDERED page 803 and implemented, so
        // this constant was already contradicted inside this tree. It is the TENTH
        // fictional enum constant to defend itself with a C2196 duplicate-case error
        // against the instruction that actually owns the opcode.
        //   constexpr uint8_t OR_B  = 0x0F;   RETIRED - is DAA / MOV.L ERs,ERd
        // STILL SUSPECT, same family, NOT read off a page (AUDIT-QUEUE): OR_W = 0x10
        // and OR_L = 0x11 sit on the SHLL/SHLR primaries, and AND_L = 0x0E sits on
        // ADDX's, which the decoder above already claims.
        constexpr uint8_t OR_W  = 0x10;    // OR word - SUSPECT, see above
        constexpr uint8_t OR_L  = 0x11;    // OR long
        constexpr uint8_t XOR_B = 0x12;    // XOR byte
        constexpr uint8_t XOR_W = 0x13;    // XOR word
        // BUG51, 2026-09-13 - RETIRED: XOR_L = 0x14, NOT_B = 0x15, NOT_W = 0x16.
        // Three more fictions of the same family as EXT_W/EXTU_B/EXTU_W (BUG37).
        // The real rows, Renesas HM Rev 3.00 Appendix A.1:
        //     OR.B  Rs,Rd  = 1 4 | rs rd   RENDERED page 803
        //     XOR.B Rs,Rd  = 1 5 | rs rd   RENDERED page 806
        //     AND.B Rs,Rd  = 1 6 | rs rd   RENDERED page 794
        // i.e. the byte counterparts of the 0x64/0x65/0x66 word group, decoded and
        // executed in h8s2350_instructions.cpp. NOT.B / NOT.W / NOT.L all share the
        // SINGLE primary 0x17 (rendered page 803) - there is no per-size primary, and
        // there is no 32-bit XOR at a bare primary at all (XOR.L is 01 F0 65, BUG-0x01F0).
        // Cost of this fiction, measured: `15 88` at 0x0023C2 is XOR.B R0L,R0L, the
        // firmware's idiom for clearing R0L before it zeroes SSR1/SCR1/SMR1. Executed
        // as "NOT.B" it left R0L = 0x5B, so SMR1 came out 0x5B - 7-bit data, 2 stop
        // bits, CKS=3 - instead of 0x00 (8N1, CKS=0), which with BRR=9 and phi=10 MHz
        // is 31250 baud exactly. MIDI OUT was never configured.
        constexpr uint8_t NOT_L = 0x17;    // NOT.B/NOT.W/NOT.L - one primary, rendered p.803
        
        // ADD Rm,Rn (register-register add) - H8S/2350 manual p.79
        constexpr uint8_t ADD_RM_RN = 0x21;  // ADD Rm,Rn
        
        // EOR Rm,Rn (register-register exclusive OR) - H8S/2350 manual p.83
        constexpr uint8_t EOR_RM_RN = 0x24;  // EOR Rm,Rn
        
        // Additional register-register ALU ops (0x20-0x2F range) - H8S/2350 manual
        constexpr uint8_t MOV_RM_RN = 0x20;  // MOV Rm,Rn
        constexpr uint8_t SUB_RM_RN = 0x22;  // SUB Rm,Rn
        constexpr uint8_t CMP_RM_RN = 0x28;  // CMP Rm,Rn
        constexpr uint8_t AND_RM_RN = 0x25;  // AND Rm,Rn
        constexpr uint8_t OR_RM_RN  = 0x26;  // OR Rm,Rn
        constexpr uint8_t XOR_RM_RN = 0x27;  // XOR Rm,Rn
        constexpr uint8_t ADD_RM_RN_EXT = 0x29;  // ADD Rm,Rn (alt)
        constexpr uint8_t SUB_RM_RN_EXT = 0x2A;  // SUB Rm,Rn (alt)
        constexpr uint8_t ROTXL_RM_RN = 0x2C;  // ROTXL Rm,Rn
        constexpr uint8_t ROTXR_RM_RN = 0x2D;  // ROTXR Rm,Rn
        constexpr uint8_t XTRCT_RM_RN = 0x2E;  // XTRCT Rm,Rn
        
        // Shift and rotate instructions
        constexpr uint8_t SHAL_B = 0x18;   // Shift arithmetic left byte
        constexpr uint8_t SHAL_W = 0x19;   // Shift arithmetic left word
        constexpr uint8_t SHAL_L = 0x1A;   // Shift arithmetic left long
        constexpr uint8_t SHAR_B = 0x1B;   // Shift arithmetic right byte
        constexpr uint8_t SHAR_W = 0x1C;   // Shift arithmetic right word
        constexpr uint8_t SHAR_L = 0x1D;   // Shift arithmetic right long
        
        // P1.6 SUBX (Subtract with extend) - H8S/2600 Manual exact opcodes (see SUBX_REG/SUBX_IMM_BASE above)
        // (duplicate constants removed)
        constexpr uint8_t SHLL_B = 0x11;   // Shift logical left byte - FIXED: was 0x1E, moved to 0x11 group per H8S manual
        constexpr uint8_t SHLL_W = 0x1F;   // Shift logical left word
        constexpr uint8_t SHLL_L = 0x20;   // Shift logical left long
        constexpr uint8_t SHLR_B = 0x21;   // Shift logical right byte
        constexpr uint8_t SHLR_W = 0x22;   // Shift logical right word
        constexpr uint8_t SHLR_L = 0x23;   // Shift logical right long
        constexpr uint8_t ROTL_B = 0x24;   // Rotate left byte
        constexpr uint8_t ROTL_W = 0x25;   // Rotate left word
        constexpr uint8_t ROTL_L = 0x26;   // Rotate left long
        constexpr uint8_t ROTR_B = 0x27;   // Rotate right byte
        constexpr uint8_t ROTR_W = 0x28;   // Rotate right word
        constexpr uint8_t ROTR_L = 0x29;   // Rotate right long
        constexpr uint8_t ROTXL_B = 0x2A;  // Rotate left with extend byte
        constexpr uint8_t ROTXL_W = 0x2B;  // Rotate left with extend word
        constexpr uint8_t ROTXL_L = 0x2C;  // Rotate left with extend long
        constexpr uint8_t ROTXR_B = 0x2D;  // Rotate right with extend byte
        constexpr uint8_t ROTXR_W = 0x2E;  // Rotate right with extend word
        constexpr uint8_t ROTXR_L = 0x2F;  // Rotate right with extend long
        
        // Branch instructions
        constexpr uint8_t BRA = 0x30;      // Branch always
        constexpr uint8_t BRN = 0x31;      // Branch never
        constexpr uint8_t BHI = 0x32;      // Branch if higher
        constexpr uint8_t BLS = 0x33;      // Branch if lower or same
        constexpr uint8_t BCC = 0x34;      // Branch if carry clear
        constexpr uint8_t BCS = 0x35;      // Branch if carry set
        constexpr uint8_t BNE = 0x36;      // Branch if not equal
        constexpr uint8_t BEQ = 0x37;      // Branch if equal
        constexpr uint8_t BVC = 0x38;      // Branch if overflow clear
        constexpr uint8_t BVS = 0x39;      // Branch if overflow set
        constexpr uint8_t BPL = 0x3A;      // Branch if plus
        constexpr uint8_t BMI = 0x3B;      // Branch if minus
        constexpr uint8_t BGE = 0x3C;      // Branch if greater or equal
        constexpr uint8_t BLT = 0x3D;      // Branch if less than
        constexpr uint8_t BGT = 0x3E;      // Branch if greater than
        constexpr uint8_t BLE = 0x3F;      // Branch if less or equal
        
        // Jump and subroutine instructions
        constexpr uint8_t JMP = 0x40;      // Jump
        constexpr uint8_t JSR = 0x41;      // Jump to subroutine
        constexpr uint8_t RTS = 0x54;      // Return from subroutine (ins04.txt)
        constexpr uint8_t POP_Rn = 0x58;   // Pop register (actual firmware opcode)
        
        // Stack operations
        constexpr uint8_t PUSH_B = 0x44;   // Push byte
        constexpr uint8_t PUSH_W = 0x45;   // Push word
        constexpr uint8_t PUSH_L = 0x46;   // Push long
        constexpr uint8_t POP_B = 0x47;    // Pop byte
        constexpr uint8_t POP_W = 0x48;    // Pop word
        constexpr uint8_t POP_L = 0x49;    // Pop long
        
        // Increment/Decrement
        constexpr uint8_t INC_B = 0x4A;    // Increment byte
        constexpr uint8_t INC_W = 0x4B;    // Increment word
        constexpr uint8_t INC_L = 0x4C;    // Increment long
        constexpr uint8_t DEC_B = 0x4D;    // Decrement byte
        constexpr uint8_t DEC_W = 0x4E;    // Decrement word
        constexpr uint8_t DEC_L = 0x4F;    // Decrement long
        
        // Clear and test
        // 2026-09-17, BUG81. ALL SIX WERE FICTION AND ARE RETIRED.
        // THERE IS NO `CLR` AND NO `TST` INSTRUCTION IN THE H8S/2350.
        // A sweep of the manual finds `CLRMAC` (and Appendix A.2 marks even that
        // "Cannot be used in the H8S/2350 Group") and 56 occurrences of the letters
        // TST, every one of them `TSTR`, the TPU timer-start register. The idiom
        // this part uses to zero a register is `SUB.B Rd,Rd` / `XOR.B Rd,Rd`, which
        // is what the firmware itself writes at 0x0023C2 and 0x002026.
        //
        // The six primaries, off RENDERED PDF pages 799 (printed "763") and 802
        // (printed "766") for the encodings and 775/776 for operation and flags:
        //     0x50 = MULXU.B Rs,Rd     0x51 = DIVXU.B Rs,Rd
        //     0x52 = MULXU.W Rs,ERd    0x53 = DIVXU.W Rs,ERd     (all TWO bytes)
        //     0x42 = BHI d:8           (BUG62, the Bcc group)
        //     0x55 = BSR d:8           (BUG62, RENDERED page 798)
        // `TST_W`'s own comment confessed the method: "moved to avoid RTS collision"
        // - a value CHOSEN TO FIT A TABLE, not read from a page.
        //
        //   constexpr uint8_t CLR_B = 0x50;   RETIRED - is MULXU.B Rs,Rd
        //   constexpr uint8_t CLR_W = 0x51;   RETIRED - is DIVXU.B Rs,Rd
        //   constexpr uint8_t CLR_L = 0x52;   RETIRED - is MULXU.W Rs,ERd
        //   constexpr uint8_t TST_B = 0x53;   RETIRED - is DIVXU.W Rs,ERd
        //   constexpr uint8_t TST_W = 0x42;   RETIRED - is BHI d:8
        //   constexpr uint8_t TST_L = 0x55;   RETIRED - is BSR d:8
        
        // System instructions
        constexpr uint8_t NOP = 0x00;      // No operation (0x0000 actual H8S NOP)
        constexpr uint8_t SLEEP = 0x43;    // Sleep (moved to avoid TRAPA collision)
        constexpr uint8_t TRAPA = 0x57;    // Trap always (H8S/2350 manual compliant) 
        constexpr uint8_t RTE = 0x56;      // Return from exception - CONFLICTS with PUSH, need resolution
        
        // Extended instructions
        // 2026-09-13, BUG37. THESE THREE WERE FICTION AND ARE RETIRED.
        // Renesas H8S/2350 HM Rev 3.00, Appendix A.1, RENDERED PDF page 806
        // (printed "page 770 of 988") and the OR/AND rows beside it:
        //     0x64 = OR.W Rs,Rd     0x65 = XOR.W Rs,Rd     0x66 = AND.W Rs,Rd
        // The real EXTS/EXTU are 0x17-prefixed forms, not bare 0x64-0x66. The
        // three constants below claimed 0x64/0x65/0x66 for "extend" and their
        // dispatch cases blocked the logical group from ever being added -
        // exactly the SWAP = 0x6A mistake recorded a few lines further down in
        // the executor. Measured: `[UNKNOWN-OPCODE] @0x004E24: 65 80 73` fired
        // 6,164 times in one run; it is XOR.W E0,R0.
        //   constexpr uint8_t EXT_W  = 0x64;   RETIRED - is OR.W  Rs,Rd
        //   constexpr uint8_t EXTU_B = 0x65;   RETIRED - is XOR.W Rs,Rd
        //   constexpr uint8_t EXTU_W = 0x66;   RETIRED - is AND.W Rs,Rd
        //
        // STILL SUSPECT, same family, NOT yet re-read off a rendered page - do
        // not trust them and do not add handlers keyed on them (AUDIT-QUEUE):
        //   EXT_B = 0x63 : 0x63 is BTST Rn,Rd, and `63 80 47` shows up live.
        //   LINK / UNLK  : LINK and UNLK are not in this part's instruction set;
        //                  0x67 is BST/BIST (implemented) and 0x68 is MOV.B @ERs,Rd.
        constexpr uint8_t EXT_B = 0x63;    // SUSPECT - see above
        constexpr uint8_t LINK = 0x67;     // SUSPECT - see above
        constexpr uint8_t UNLK = 0x68;     // SUSPECT - see above
        
        // Exchange - EXG = 0x69 REMOVED: conflicts with MOV.B Rs,@ERn (0x69 per H8S manual)
        // constexpr uint8_t EXG = 0x69;      // Exchange registers - DISABLED  
        constexpr uint8_t SWAP = 0x6A;     // Swap bytes in word
        
        // Bit operations
        constexpr uint8_t BSET = 0x6B;     // Bit set
        constexpr uint8_t BCLR = 0x6C;     // Bit clear
        constexpr uint8_t BNOT = 0x6D;     // Bit not
        constexpr uint8_t BTST = 0x6E;     // Bit test
        
        // Conditional execution
        constexpr uint8_t MOVT = 0x6F;     // Move if true
        
        // Special instructions
        constexpr uint8_t LDC = 0x71;      // Load control register
        constexpr uint8_t STC = 0x72;      // Store control register
        constexpr uint8_t ANDC = 0x73;     // AND control register
        constexpr uint8_t ORC = 0x74;      // OR control register
        constexpr uint8_t XORC = 0x75;     // XOR control register
        constexpr uint8_t LDTLB = 0x76;    // Load TLB
        constexpr uint8_t LDRE = 0x77;     // Load repeat end
        constexpr uint8_t LDRC = 0x78;     // Load repeat count
        constexpr uint8_t STRE = 0x79;     // Store repeat end
        constexpr uint8_t STRC = 0x7A;     // Store repeat count
    }
    
    // Addressing modes
    namespace H8S2350AddressingMode
    {
        constexpr uint8_t REGISTER_DIRECT = 0x00;      // Rn
        constexpr uint8_t REGISTER_INDIRECT = 0x01;    // @Rn
        constexpr uint8_t REGISTER_INDIRECT_POST = 0x02; // @Rn+
        constexpr uint8_t REGISTER_INDIRECT_PRE = 0x03;  // @-Rn
        constexpr uint8_t REGISTER_INDIRECT_DISP = 0x04; // @(d:16, Rn)
        constexpr uint8_t REGISTER_INDIRECT_INDEX = 0x05; // @(d:8, Rn, Rm)
        constexpr uint8_t ABSOLUTE_ADDRESS = 0x06;     // @aa:8, @aa:16, @aa:24
        constexpr uint8_t IMMEDIATE = 0x07;            // #xx:8, #xx:16, #xx:32
        constexpr uint8_t PC_RELATIVE = 0x08;          // @(d:8, PC)
        constexpr uint8_t PC_RELATIVE_INDEX = 0x09;    // @(d:8, PC, Rn)
        constexpr uint8_t REGISTER_LIST = 0x0A;        // Rn-Rm
        constexpr uint8_t MEMORY_INDIRECT = 0x0B;      // @@aa:8
    }
    
    // Execution result structure (fw29.txt GPT5 design)
    struct ExecResult
    {
        bool pc_overridden;     // true if instruction changed PC (branch/jump)
        uint32_t next_pc;       // new PC value if overridden
        uint32_t cycles;        // actual cycles consumed
        
        ExecResult() : pc_overridden(false), next_pc(0), cycles(0) {}
        ExecResult(uint32_t c) : pc_overridden(false), next_pc(0), cycles(c) {}
        ExecResult(uint32_t pc, uint32_t c) : pc_overridden(true), next_pc(pc), cycles(c) {}
    };

    // Instruction structure
    struct H8S2350Instruction
    {
        uint8_t opcode;
        uint8_t addressing_mode;
        uint8_t source_operand;
        uint8_t destination_operand;
        uint32_t immediate_value;
        uint16_t displacement;
        uint32_t decoded_pc;   // start PC of this instruction at decode time (24-bit masked)
        uint8_t size;  // 0=byte, 1=word, 2=long
        uint32_t baseCycles; // base cycle count for instruction
        std::string mnemonic;
        
        H8S2350Instruction() : opcode(0), addressing_mode(0), source_operand(0), 
                              destination_operand(0), immediate_value(0), 
                              displacement(0), decoded_pc(0), size(0), baseCycles(1), mnemonic("") {}
    };
    
    // Forward declaration
    class H8S2350Emulator;

    // Instruction decoder (fw29.txt GPT5 design - PC + memory context)
    class H8S2350InstructionDecoder
    {
    public:
        // New PC-based decode method (GPT5 recommended)
        H8S2350Instruction decode(H8S2350Emulator& emulator, uint32_t pc);
        
        // Legacy methods (compatibility)
        static H8S2350Instruction decode(uint16_t instruction);
        static std::string getMnemonic(const H8S2350Instruction& instr);
        static uint8_t getInstructionSize(const H8S2350Instruction& instr);
        static bool isValidInstruction(uint16_t instruction);
        
    private:
        static uint8_t extractOpcode(uint16_t instruction);
        static uint8_t extractAddressingMode(uint16_t instruction);
        static uint8_t extractSourceOperand(uint16_t instruction);
        static uint8_t extractDestinationOperand(uint16_t instruction);
        
        // GPT5 fw29.txt helper methods
        static uint8_t calculateInstructionSize(H8S2350Emulator& emulator, uint32_t pc, uint16_t instruction);
        static uint32_t calculateBaseCycles(uint8_t opcode, uint8_t addressing_mode);
    };
    
    // Instruction executor (fw29.txt GPT5 design)
    class H8S2350InstructionExecutor
    {
    public:
        // New execute method returns ExecResult (GPT5 recommended)
        ExecResult execute(H8S2350Emulator& emulator, const H8S2350Instruction& instruction, uint32_t pc0);
        
        // Legacy method (compatibility)
        static bool execute(const H8S2350Instruction& instruction, 
                           class H8S2350Emulator* emulator);
        
    private:
        // Move instructions
        static bool executeMove(const H8S2350Instruction& instruction, 
                               class H8S2350Emulator* emulator);
        
        // Arithmetic instructions
        static bool executeAdd(const H8S2350Instruction& instruction, 
                              class H8S2350Emulator* emulator);
        static bool executeAddRmRn(const H8S2350Instruction& instruction, 
                                   class H8S2350Emulator* emulator);
        static bool executeEorRmRn(const H8S2350Instruction& instruction, 
                                   class H8S2350Emulator* emulator);
        static bool executeRegRegAlu(const H8S2350Instruction& instruction, 
                                     class H8S2350Emulator* emulator);
        static bool executeSubtract(const H8S2350Instruction& instruction, 
                                   class H8S2350Emulator* emulator);
        static bool executeCompare(const H8S2350Instruction& instruction, 
                                  class H8S2350Emulator* emulator);
        
        // Logical instructions
        static bool executeAnd(const H8S2350Instruction& instruction, 
                              class H8S2350Emulator* emulator);
        static bool executeOr(const H8S2350Instruction& instruction, 
                             class H8S2350Emulator* emulator);
        static bool executeXor(const H8S2350Instruction& instruction, 
                              class H8S2350Emulator* emulator);
        static bool executeNot(const H8S2350Instruction& instruction, 
                              class H8S2350Emulator* emulator);
        
        // Shift and rotate instructions
        static bool executeShiftLeft(const H8S2350Instruction& instruction, 
                                    class H8S2350Emulator* emulator);
        static bool executeShiftRight(const H8S2350Instruction& instruction, 
                                     class H8S2350Emulator* emulator);
        static bool executeRotateLeft(const H8S2350Instruction& instruction, 
                                     class H8S2350Emulator* emulator);
        static bool executeRotateRight(const H8S2350Instruction& instruction, 
                                      class H8S2350Emulator* emulator);
        
        // Branch instructions
        static bool executeBranch(const H8S2350Instruction& instruction, 
                                 class H8S2350Emulator* emulator);

        // Bcc d:8 (0x40-0x4F) conditional branch
        static bool executeBcc8(const H8S2350Instruction& instruction,
                                          H8S2350Emulator* emulator);

        // 0x58 = Bcc d:16 (16-bit displacement conditional branch) - Renesas p.279/280
        static bool executeBcc16(const H8S2350Instruction& instruction,
                                 H8S2350Emulator* emulator);

        // 0x1B = DEC.W/DEC.L/SUBS group (Renesas p.115/253/283)
        static bool execute0x1BInstruction(const H8S2350Instruction& instruction,
                                          H8S2350Emulator* emulator);
        static bool executeJump(const H8S2350Instruction& instruction, 
                               class H8S2350Emulator* emulator);

        // 0x5A = JMP @aa:24 (24-bit absolute jump)
        static bool executeJMP_AA24(const H8S2350Instruction& instruction, class H8S2350Emulator* emulator);
        static bool executeJumpSubroutine(const H8S2350Instruction& instruction, 
                                         class H8S2350Emulator* emulator);
        static bool executeReturnFromSubroutine(const H8S2350Instruction& instruction, 
                                               class H8S2350Emulator* emulator);
        static bool executeReturnFromException(const H8S2350Instruction& instruction, 
                                             class H8S2350Emulator* emulator);
        
        // Extended instruction support
        static bool executeExtendedALU(const H8S2350Instruction& instruction,
                                      class H8S2350Emulator* emulator);
        
        // BCD correction instructions - H8S/2350 compatible
        static bool executeDAA(const H8S2350Instruction& instruction,
                              class H8S2350Emulator* emulator);
        static bool executeDAS(const H8S2350Instruction& instruction,
                              class H8S2350Emulator* emulator);
        
        // i10.txt: 0x7A Advanced Mode 24-bit absolute addressing group
        static bool execute0x7AGroup(const H8S2350Instruction& instruction,
                                     class H8S2350Emulator* emulator);

        // Handle 0xF9 instruction (MOV.B Rs, Rd)
        static bool execute0xF9Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // MOV.B/MOV.W Rs,Rd (0x0C/0x0D) - Renesas p.284/285
        // BUG93: ADD.B Rs,Rd = 08 rs rd (RENDERED page 774, printed 738).
        static bool executeADD_W_REG_REG(const H8S2350Instruction& instruction,
                                         class H8S2350Emulator* emulator);   // BUG105: 09 rs rd
        static bool executeADD_B_REG_REG(const H8S2350Instruction& instruction,
                                         class H8S2350Emulator* emulator);
        static bool executeMOV_B_REG_REG(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool executeMOV_W_REG_REG(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // AND.B #xx:8, Rd (0xE0-0xEF) - Renesas p.279
        static bool executeAND_B_IMM8(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // 0xC0-0xCF = OR.B #xx:8,Rd ; 0xD0-0xDF = XOR.B #xx:8,Rd (Renesas h8s2600 p.178)
        static bool executeOR_B_IMM8(const H8S2350Instruction& instruction, class H8S2350Emulator* emulator);
        static bool executeXOR_B_IMM8(const H8S2350Instruction& instruction, class H8S2350Emulator* emulator);

        // CMP.B #xx:8, Rd (0xA0-0xAF) - Renesas p.107/299
        static bool executeCMP_B_IMM8(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // 0x0A = INC.B Rd / ADD.L ERs,ERd (Renesas p.66/132/283)
        static bool execute0x0AInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // 0x0B = ADDS / INC.W / INC.L group (Renesas p.67/133/134/279)
        static bool execute0x0BInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle register-indirect MOV.B instructions
        static bool execute0x68Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x69Family(const H8S2350Instruction& instruction,
                                     class H8S2350Emulator* emulator);
        static bool execute0x6CInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x6EInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x78Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x79Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x7DInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x7EInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x5BInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x5DInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x39Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x57Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);
        static bool execute0x00Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x07 instruction (MOV.W @(d:16, Rn), Rd)
        static bool execute0x07Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x08 instruction (MOV.W Rs, Rd)
        static bool execute0x08Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // ===== 0x01 PREFIX EXECUTORS =====
        // Handle 0x01 prefix: LDC #imm8, EXR/CCR
        static bool execute0x01LdcImmediate(const H8S2350Instruction& instruction,
                                            class H8S2350Emulator* emulator);
        // Handle 0x01 prefix: MOV.L extended (various addressing modes)
        static bool execute0x01MovLExtended(const H8S2350Instruction& instruction,
                                            class H8S2350Emulator* emulator);
        // Handle 0x01 prefix: STM.L (ERn..ERn+k),@-SP / LDM.L @SP+,(ERn..ERn+k)
        static bool execute0x01StmLdm(const H8S2350Instruction& instruction,
                                      class H8S2350Emulator* emulator);

        // ===== 0x59 EXECUTOR =====
        // Execute 0x59 = ROTXL (Rotate Left Through Carry)
        static bool execute0x59Instruction(const H8S2350Instruction& instruction,
                                           class H8S2350Emulator* emulator);

        // ===== 0x5A EXECUTOR =====
        // Execute 0x5A = JMP @aa:24 (24-bit absolute jump)
        static bool execute0x5AInstruction(const H8S2350Instruction& instruction,
                                           class H8S2350Emulator* emulator);

        // ===== LEGACY MIGRATION EXECUTORS =====
        // Migrated from legacy switch statement to new system

        // Handle 0x02 instruction (MOV.W #imm16, Rd)
        static bool execute0x02Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x03 instruction (MOV.L #imm32, ERd)
        static bool execute0x03Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x04 instruction (MOV.B Rd, @(d:16, Rn))
        static bool execute0x04Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x05 instruction (MOV.B @(d:16, Rn), Rd)
        static bool execute0x05Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x06 instruction (MOV.W Rd, @(d:16, Rn))
        static bool execute0x06Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x6A instruction (0x6A unary register group: EXTU/EXTS/INC/DEC/etc.)
        static bool execute0x6AInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // ===== REGISTER OPERATIONS EXECUTORS (0x20-0x7F) =====
        // These will be implemented as separate executor methods

        // Handle 0x20 instruction (ADD #imm8, Rn)
        static bool execute0x20Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x21 instruction (ADD Rm, Rn)
        static bool execute0x21Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x28 instruction (CMP #imm8, Rn)
        static bool execute0x28Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x30 instruction (AND #imm8, Rn)
        static bool execute0x30Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x38 instruction (SHLL Rn)
        static bool execute0x38Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x40 instruction (ADDQ #imm3, Rn)
        static bool execute0x40Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x46 instruction (MOV Rm, Rn)
        static bool execute0x46Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x47 instruction (MOV.L ERm, ERn)
        static bool execute0x47Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x54 instruction (RTS - Return from Subroutine) - FIXED from INC.L
        static bool execute0x54Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x56 instruction (PUSH Rn)
        static bool execute0x56Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x58 instruction (POP Rn)
        static bool execute0x58Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x5C instruction (MOV.W @ERm, Rn)
        static bool execute0x5CInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x5E instruction (MOV.L @ERm, ERn)
        static bool execute0x5EInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x60 instruction (ANDC #imm8, CCR)
        static bool execute0x60Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x61-0x66 = Register-register logical ops (AND/OR/XOR .B/.W)
        static bool execute0x61_66LogicalRR(const H8S2350Instruction& instruction,
                                             class H8S2350Emulator* emulator);

        // Handle 0x67 = Bit manipulation BAND/BIAND @ERd / @aa:16 / @aa:32
        static bool execute0x67Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x6B instruction (LDMS ERd, @ERs)
        static bool execute0x6BInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x6D instruction (MOVU.W @ERm+, Rn)
        static bool execute0x6DInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x6F instruction (MOVU.L @ERm+, ERn)
        static bool execute0x6FInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x70 instruction (MOVA.L @aa, ERn)
        static bool execute0x70Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x71 instruction (MOVA.L @(disp, PC), ERn)
        static bool execute0x71Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x72 instruction (MOVA.L @(disp, ERm), ERn)
        static bool execute0x72Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x73 instruction (MOVA.L @(ERm, ERk), ERn)
        static bool execute0x73Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x74 instruction (LDC @ERn, VBR)
        static bool execute0x74Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x76 instruction (LDC ERn, VBR)
        static bool execute0x76Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x77 instruction (BOR #imm3, @ERn)
        static bool execute0x77Instruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x7B instruction (STC VBR, ERn)
        static bool execute0x7BInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0x7C instruction (STC VBR, @ERn)
        static bool execute0x7CInstruction(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // ===== MEMORY OPERATIONS EXECUTORS (0x80-0xFF) =====

        // Handle 0x80-0x8F range (MOV.B #imm8, Rn)
        static bool executeMOV_B_IMM8_Rn(const H8S2350Instruction& instruction,
                                        class H8S2350Emulator* emulator);

        // Handle 0xA0-0xA7 range (BRA @label)
        static bool executeBRA_AT_LABEL(const H8S2350Instruction& instruction,
                                       class H8S2350Emulator* emulator);

        // Handle 0xA8-0xAF range (JSR @aa)
        static bool executeJSR_AT_AA(const H8S2350Instruction& instruction,
                                    class H8S2350Emulator* emulator);

        // Handle 0xE8 instruction (XOR.B Rn, Rm)
        static bool executeXOR_B_RN_RM(const H8S2350Instruction& instruction,
                                      class H8S2350Emulator* emulator);

        // Handle 0xE9 instruction (XOR.W Rn, Rm)
        static bool executeXOR_W_RN_RM(const H8S2350Instruction& instruction,
                                      class H8S2350Emulator* emulator);

        // Handle 0xF4 instruction (BCLR #imm3, @(disp, ERn))
        static bool executeBCLR_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                class H8S2350Emulator* emulator);

        // 0x72 = BCLR #xx:3, Rd register-direct (Renesas p.79/280)
        static bool executeBCLR_IMM3_REG(const H8S2350Instruction& instruction,
                                          class H8S2350Emulator* emulator);

        // Handle 0xF6 instruction (BAND #imm3, @(disp, ERn))
        static bool executeBAND_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                class H8S2350Emulator* emulator);

        // Handle 0xF7 instruction (BOR #imm3, @(disp, ERn))
        static bool executeBOR_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                               class H8S2350Emulator* emulator);

        // Handle 0xF8 instruction (BXOR #imm3, @(disp, ERn))
        static bool executeBXOR_IMM3_AT_DISP_ERN(const H8S2350Instruction& instruction,
                                                class H8S2350Emulator* emulator);

        // Handle 0xFC instruction (BTST #imm3, Rn)
        static bool executeBTST_IMM3_RN(const H8S2350Instruction& instruction,
                                       class H8S2350Emulator* emulator);

        // Handle 0xFE instruction (BXOR #imm3, Rn)
        static bool executeBXOR_IMM3_RN(const H8S2350Instruction& instruction,
                                       class H8S2350Emulator* emulator);

        // Stack instructions
        static bool executePush(const H8S2350Instruction& instruction,
                               class H8S2350Emulator* emulator);
        static bool executePop(const H8S2350Instruction& instruction,
                              class H8S2350Emulator* emulator);
        
                 // System instructions
                 static bool executeNop(const H8S2350Instruction& instruction,
                                       class H8S2350Emulator* emulator);
                 static bool executeNopFF(const H8S2350Instruction& instruction,
                                         class H8S2350Emulator* emulator);
                 static bool executeSleep(const H8S2350Instruction& instruction,
                                         class H8S2350Emulator* emulator);
                 static bool executeTrap(const H8S2350Instruction& instruction,
                                        class H8S2350Emulator* emulator);
         
         // Additional instructions
         static bool executeIncrement(const H8S2350Instruction& instruction, 
                                    class H8S2350Emulator* emulator);
         static bool executeDecrement(const H8S2350Instruction& instruction, 
                                    class H8S2350Emulator* emulator);
         static bool executeClear(const H8S2350Instruction& instruction, 
                                class H8S2350Emulator* emulator);
         static bool executeTest(const H8S2350Instruction& instruction, 
                               class H8S2350Emulator* emulator);
         static bool executeExtend(const H8S2350Instruction& instruction, 
                                 class H8S2350Emulator* emulator);
         static bool executeExchange(const H8S2350Instruction& instruction, 
                                   class H8S2350Emulator* emulator);
         static bool executeSwap(const H8S2350Instruction& instruction, 
                               class H8S2350Emulator* emulator);
    };
}
// Global trace toggle for execute functions
namespace MS2000 {
void setGlobalInsnTrace(bool v);
}
