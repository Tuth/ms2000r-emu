#pragma once

#include <cstdint>

namespace MS2000 {

// H8S/2350 Memory map (updated based on MAME H8S/2357 + MS2000 schematic page 14)
namespace H8S2350MemoryMap
{
    // Flash ROM (firmware storage) - MBM29LV800B = 8 Mbit = 1 MB
    static const uint32_t FLASH_START = 0x00000000;
    static const uint32_t FLASH_SIZE = 0x100000;    // 1 MB Flash ROM (MBM29LV800B)

    // CPU SRAM (V53C16256LK = 256K x 16 = 512 KB) at 0x00100000
    static const uint32_t CPU_RAM_START = 0x00100000;
    static const uint32_t CPU_RAM_SIZE = 0x80000;    // 512 KB CPU SRAM (V53C16256LK = 256K x 16)

    // External Memory window for bus mapping (24-bit bus = 16 MB per area in Advanced Mode)
    // Area 0 (CS0-CS3) covers 0x00000000-0x00FFFFFF (16 MB)
    // CS0: Flash 1MB at 0x00000000-0x000FFFFF
    // CS1: SRAM 512KB at 0x00100000-0x0017FFFF
    // CS2/CS3: LED drivers, panel regs at 0x00400000-0x007FFFFF
    static const uint32_t EXTERNAL_MEMORY_START = 0x00000000;
    static const uint32_t EXTERNAL_MEMORY_SIZE = 0x1000000;  // 16 MB (full Area 0)

    // RAM (internal) - 8KB like H8S/2357
    static const uint32_t RAM_START = 0xFFF80000;
    static const uint32_t RAM_SIZE = 0x2000;        // 8KB RAM
    
    // I/O Space (MMIO) - Full 4GB MMIO space
    static const uint32_t I_O_START = 0xFF000000;
    static const uint32_t I_O_SIZE = 0x1000000;     // 16MB I/O space
    
    // Advanced Mode Memory Areas (8 areas, 16MB total)
    namespace AdvancedMode
    {
        // Area 0: Flash ROM (0x00000000 - 0x00FFFFFF)
        static const uint32_t AREA0_START = 0x00000000;
        static const uint32_t AREA0_SIZE = 0x1000000;   // 16MB
        
        // Area 1: External Memory (0x10000000 - 0x1FFFFFFF)
        static const uint32_t AREA1_START = 0x10000000;
        static const uint32_t AREA1_SIZE = 0x1000000;   // 16MB
        
        // Area 2: Reserved (0x20000000 - 0x2FFFFFFF)
        static const uint32_t AREA2_START = 0x20000000;
        static const uint32_t AREA2_SIZE = 0x1000000;   // 16MB
        
        // Area 3: Reserved (0x30000000 - 0x3FFFFFFF)
        static const uint32_t AREA3_START = 0x30000000;
        static const uint32_t AREA3_SIZE = 0x1000000;   // 16MB
        
        // Area 4: Reserved (0x40000000 - 0x4FFFFFFF)
        static const uint32_t AREA4_START = 0x40000000;
        static const uint32_t AREA4_SIZE = 0x1000000;   // 16MB
        
        // Area 5: Reserved (0x50000000 - 0x5FFFFFFF)
        static const uint32_t AREA5_START = 0x50000000;
        static const uint32_t AREA5_SIZE = 0x1000000;   // 16MB
        
        // Area 6: Reserved (0x60000000 - 0x6FFFFFFF)
        static const uint32_t AREA6_START = 0x60000000;
        static const uint32_t AREA6_SIZE = 0x1000000;   // 16MB
        
        // Area 7: I/O Space (0x70000000 - 0x7FFFFFFF)
        static const uint32_t AREA7_START = 0x70000000;
        static const uint32_t AREA7_SIZE = 0x1000000;   // 16MB
    }
    
    // H8S/2350 I/O register addresses (based on hardware specification)
    // Uses 16-bit addressing as per H8S2350 datasheet
    namespace IORegisters {
        // CPU Control Registers
        static const uint32_t SYSCR = 0xFF39;        // System Control Register
        static const uint32_t MDCR = 0xFF3B;         // Mode Control Register (Read-Only)
        
        // Interrupt Control Unit (ICU) Registers
        static const uint32_t ISCRH = 0xFF2C;        // IRQ Sense Control High
        static const uint32_t ISCRL = 0xFF2D;        // IRQ Sense Control Low
        static const uint32_t IER = 0xFF2E;          // IRQ Enable Register
        static const uint32_t ISR = 0xFF2F;          // IRQ Status Register
        
        // Interrupt Priority Registers (IPR A-K)
        static const uint32_t IPRA = 0xFEC4;         // Interrupt Priority Register A
        static const uint32_t IPRB = 0xFEC5;         // Interrupt Priority Register B
        static const uint32_t IPRC = 0xFEC6;         // Interrupt Priority Register C
        static const uint32_t IPRD = 0xFEC7;         // Interrupt Priority Register D
        static const uint32_t IPRE = 0xFEC8;         // Interrupt Priority Register E
        static const uint32_t IPRF = 0xFEC9;         // Interrupt Priority Register F
        static const uint32_t IPRG = 0xFECA;         // Interrupt Priority Register G
        static const uint32_t IPRH = 0xFECB;         // Interrupt Priority Register H
        static const uint32_t IPRI = 0xFECC;         // Interrupt Priority Register I
        static const uint32_t IPRJ = 0xFECD;         // Interrupt Priority Register J (DMAC, SCI0)
        static const uint32_t IPRK = 0xFECE;         // Interrupt Priority Register K (SCI1)
        
        // Bus Controller Registers
        static const uint32_t ABWCR = 0xFED0;        // Area Bus Width Control Register
        static const uint32_t ASTCR = 0xFED1;        // Access State Control Register
        static const uint32_t WCRH = 0xFED2;         // Wait Control Register High
        static const uint32_t WCRL = 0xFED3;         // Wait Control Register Low
        static const uint32_t BCRH = 0xFED4;         // Bus Control Register High
        static const uint32_t BCRL = 0xFED5;         // Bus Control Register Low
        static const uint32_t MCR = 0xFED6;          // Memory Control Register
        static const uint32_t DRAMCR = 0xFED7;       // DRAM Control Register
        static const uint32_t RTCNT = 0xFED8;        // Refresh Timer Counter
        static const uint32_t RTCOR = 0xFED9;        // Refresh Timer Constant Register
        
        // SCI0 (Serial Communication Interface) - MIDI
        static const uint32_t SCI0_SMR = 0xFF78;     // SCI0 Serial Mode Register
        static const uint32_t SCI0_BRR = 0xFF79;     // SCI0 Bit Rate Register
        static const uint32_t SCI0_SCR = 0xFF7A;     // SCI0 Serial Control Register
        static const uint32_t SCI0_TDR = 0xFF7B;     // SCI0 Transmit Data Register
        static const uint32_t SCI0_SSR = 0xFF7C;     // SCI0 Serial Status Register
        static const uint32_t SCI0_RDR = 0xFF7D;     // SCI0 Receive Data Register
        static const uint32_t SCI0_SCMR = 0xFF7E;    // SCI0 Serial Control Mode Register
        
        // SCI1 (Serial Communication Interface) - fw8.txt Multi-SCI
        static const uint32_t SCI1_SMR = 0xFF80;     // SCI1 Serial Mode Register
        static const uint32_t SCI1_BRR = 0xFF81;     // SCI1 Bit Rate Register
        static const uint32_t SCI1_SCR = 0xFF82;     // SCI1 Serial Control Register
        static const uint32_t SCI1_TDR = 0xFF83;     // SCI1 Transmit Data Register
        static const uint32_t SCI1_SSR = 0xFF84;     // SCI1 Serial Status Register
        static const uint32_t SCI1_RDR = 0xFF85;     // SCI1 Receive Data Register
        static const uint32_t SCI1_SCMR = 0xFF86;    // SCI1 Serial Control Mode Register
        
        // SCI2 (Serial Communication Interface) - fw8.txt Multi-SCI
        static const uint32_t SCI2_SMR = 0xFF88;     // SCI2 Serial Mode Register
        static const uint32_t SCI2_BRR = 0xFF89;     // SCI2 Bit Rate Register
        static const uint32_t SCI2_SCR = 0xFF8A;     // SCI2 Serial Control Register
        static const uint32_t SCI2_TDR = 0xFF8B;     // SCI2 Transmit Data Register
        static const uint32_t SCI2_SSR = 0xFF8C;     // SCI2 Serial Status Register
        static const uint32_t SCI2_RDR = 0xFF8D;     // SCI2 Receive Data Register
        static const uint32_t SCI2_SCMR = 0xFF8E;    // SCI2 Serial Control Mode Register
        
        // Port registers (keeping existing MAME-based mapping for compatibility)
        static const uint32_t PORT1_DR = 0xFFFF60;   // Port 1 Data Register
        static const uint32_t PORT2_DR = 0xFFFF61;   // Port 2 Data Register
        static const uint32_t PORT3_DR = 0xFFFF62;   // Port 3 Data Register
        static const uint32_t PORT5_DR = 0xFFFF64;   // Port 5 Data Register
        static const uint32_t PORT6_DR = 0xFFFF65;   // Port 6 Data Register
        static const uint32_t PORTA_DR = 0xFFFF69;   // Port A Data Register
        static const uint32_t PORTB_DR = 0xFFFF6A;   // Port B Data Register
        static const uint32_t PORTC_DR = 0xFFFF6B;   // Port C Data Register
        static const uint32_t PORTD_DR = 0xFFFF6C;   // Port D Data Register
        static const uint32_t PORTE_DR = 0xFFFF6D;   // Port E Data Register
        static const uint32_t PORTF_DR = 0xFFFF6E;   // Port F Data Register (LCD)
        static const uint32_t PORTG_DR = 0xFFFF6F;   // Port G Data Register
        
        // Port read registers
        static const uint32_t PORT1_R = 0xFFFF50;    // Port 1 Read
        static const uint32_t PORT2_R = 0xFFFF51;    // Port 2 Read
        static const uint32_t PORT3_R = 0xFFFF52;    // Port 3 Read
        static const uint32_t PORT4_R = 0xFFFF53;    // Port 4 Read
        static const uint32_t PORT5_R = 0xFFFF54;    // Port 5 Read
        static const uint32_t PORT6_R = 0xFFFF55;    // Port 6 Read
        static const uint32_t PORTA_R = 0xFFFF59;    // Port A Read
        static const uint32_t PORTB_R = 0xFFFF5A;    // Port B Read
        static const uint32_t PORTC_R = 0xFFFF5B;    // Port C Read
        static const uint32_t PORTD_R = 0xFFFF5C;    // Port D Read
        static const uint32_t PORTE_R = 0xFFFF5D;    // Port E Read
        static const uint32_t PORTF_R = 0xFFFF5E;    // Port F Read (LCD)
        static const uint32_t PORTG_R = 0xFFFF5F;    // Port G Read
        
        // ADC (Analog-to-Digital Converter)
        static const uint32_t ADC_ADDR8 = 0xFFFF90;  // ADC Data Register (8-bit)
        static const uint32_t ADC_ADCSR = 0xFFFF98;  // ADC Control/Status Register
        static const uint32_t ADC_ADCR = 0xFFFF99;   // ADC Control Register
        
        // Timer registers
        static const uint32_t TIMER16_TSTR = 0xFFFFC0; // Timer16 Start Register
        static const uint32_t TIMER16_TSYR = 0xFFFFC1; // Timer16 Synchronization Register
    }
}

} // namespace MS2000
