#pragma once
#include <cstring>

// Guard for contracts utility functions that require emulator access
#define H8S2350_EMULATOR_H_INCLUDED

// PC write tracing default is OFF; define TRACE_PC_WRITES=1 in build flags to enable

#include <cstdint>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <bitset>
#include <unordered_map>  // For stack taint tracking
#include <mutex>          // BUG108: MIDI IN from the host thread
#include <deque>
#include <atomic>         // GUI-2: panel switches from the GUI thread
#include <immintrin.h>  // Add SIMD support
#include "h8s2350_memory_map.h"
#include "h8s2350_interrupt_system.h"
#include "h8s2350_peripherals.h"
#include "h8s2350_instructions.h"
#include "h8s2350_contracts.h"
#include "lcd_display.h"
#include "real_lcd_display.h"
#include "midi_interface.h"
#include "flash_rom.h"
#include "io_ports.h"
#include "ad_converter.h"
#include "timer_module.h"
#include "sci_module.h"
#include "h8s2350_v2_1.h"  // Add PanelIF interface
#include "ms2000_mp_stub.h"
#include "ms2000_panel_adc.h"  // Add Panel ADC system
#include "ms2000_switch_matrix.h"  // Add Switch Matrix system
#include "ms2000_led_matrix.h"    // Add LED Matrix system
#include "ms2000_midi_interface_simple.h" // Add MIDI Interface
#include "vector_table_tracker.h"  // Add Vector Table Tracker
#include "ms2k_lcd_adapter.h"       // Add MS2000 LCD GPIO adapter
#include "replay_logger.h"          // Replay Debugger
#include "dsp56362_emulator.h"      // BUG105: IC17, the real part (upstream library + Motorola ROM)

namespace MS2000 {

// Forward declarations
class SCIModule;
class TimerModule;
class ADConverter;
class IOPorts;
class FlashROM;
class MIDIInterface;
class HostPortInterface;
class LCDDisplay;
class H8S2350InterruptSystem;
class H8S2350PeripheralEmulator;

// LCD callback system for firmware communication
using LCDWriteCallback = std::function<void(uint8_t value, bool is_data)>;
using LCDReadCallback = std::function<uint8_t(bool is_data)>;

// CPU Mode Detection (ins02.txt + ins03.txt) - NORMAL16 vs ADV24 addressing
enum class H8SCpuMode {
    NORMAL16,  // 16-bit PC, 16-bit vectors
    ADV24      // 24-bit PC, 32-bit vectors (lower 24 bits used) - MS2000 Mode 4
};

struct CpuModeConfig {
    H8SCpuMode mode = H8SCpuMode::ADV24;     // ins03.txt: MS2000 uses Mode 4 Advanced
    bool autoDetected = false;               // Set to true after detection
    uint16_t externalBusWidth = 16;          // ins03.txt: 16-bit external data bus
    uint32_t vectorTableBase = 0x00000000;   // ins03.txt: Vector table at 0x00000000
};

// TRAPA vector configuration (ins02.txt + ins03.txt)
struct TrapVectorConfig {
    bool     autoDetect = false;  // ins03.txt: Use fixed Mode 4 configuration
    uint16_t baseIndex  = 8;      // ins02.txt/ins03.txt: TRAPA base is always 8 + n (n=0..3)
    bool     locked     = true;   // ins03.txt: Mode 4 configuration is locked
    uint8_t  maxTrapNum = 3;      // ins03.txt: TRAPA only #0..#3
};

// SIMD Optimization Configuration
struct SIMDOptimizationConfig {
    bool enabled = false;                    // Enable SIMD optimizations
    bool hot_memory_enabled = true;          // Enable hot memory optimization
    bool instruction_caching_enabled = true; // Enable instruction caching
    bool vectorized_processing_enabled = true; // Enable vectorized instruction processing
    size_t cache_size = 50000;              // Instruction cache size
    size_t hot_memory_size = 0x90;          // Hot memory range size
    uint32_t hot_memory_start = 0x004000;   // Hot memory start address
    uint32_t hot_memory_end = 0x004090;     // Hot memory end address
};

// SIMD-optimized instruction cache
class SIMDInstructionCache {
private:
    static constexpr uint8_t CRITICAL_OPCODE_6B = 0x6b;
    static constexpr uint8_t FREQUENT_OPCODE_5E = 0x5e;
    static constexpr uint8_t FREQUENT_OPCODE_1B = 0x1b;
    static constexpr uint8_t FREQUENT_OPCODE_F8 = 0xf8;
    
    alignas(64) std::vector<uint8_t> m_instruction_cache;
    alignas(64) uint64_t m_cache_hits = 0;
    alignas(64) uint64_t m_cache_misses = 0;
    alignas(64) uint64_t m_fast_path_6b = 0;
    alignas(64) uint64_t m_fast_path_5e = 0;
    alignas(64) uint64_t m_fast_path_1b = 0;
    alignas(64) uint64_t m_fast_path_f8 = 0;

public:
    explicit SIMDInstructionCache(size_t cache_size) : m_instruction_cache(cache_size, 0) {}
    
    bool findInstruction(uint32_t address, uint8_t& opcode) {
        size_t index = address % m_instruction_cache.size();
        if (m_instruction_cache[index] != 0) {
            opcode = m_instruction_cache[index];
            m_cache_hits++;
            return true;
        }
        m_cache_misses++;
        return false;
    }
    
    void cacheInstruction(uint32_t address, uint8_t opcode) {
        size_t index = address % m_instruction_cache.size();
        m_instruction_cache[index] = opcode;
    }
    
    // SIMD-optimized fast path detection
    __forceinline bool isCriticalOpcode6b(uint8_t opcode) const {
        if (opcode == CRITICAL_OPCODE_6B) {
            const_cast<SIMDInstructionCache*>(this)->m_fast_path_6b++;
            return true;
        }
        return false;
    }
    
    __forceinline bool isFrequentOpcode5e(uint8_t opcode) const {
        if (opcode == FREQUENT_OPCODE_5E) {
            const_cast<SIMDInstructionCache*>(this)->m_fast_path_5e++;
            return true;
        }
        return false;
    }
    
    __forceinline bool isFrequentOpcode1b(uint8_t opcode) const {
        if (opcode == FREQUENT_OPCODE_1B) {
            const_cast<SIMDInstructionCache*>(this)->m_fast_path_1b++;
            return true;
        }
        return false;
    }
    
    __forceinline bool isFrequentOpcodeF8(uint8_t opcode) const {
        if (opcode == FREQUENT_OPCODE_F8) {
            const_cast<SIMDInstructionCache*>(this)->m_fast_path_f8++;
            return true;
        }
        return false;
    }
    
    // Statistics
    uint64_t getCacheHits() const { return m_cache_hits; }
    uint64_t getCacheMisses() const { return m_cache_misses; }
    uint64_t getFastPath6b() const { return m_fast_path_6b; }
    uint64_t getFastPath5e() const { return m_fast_path_5e; }
    uint64_t getFastPath1b() const { return m_fast_path_1b; }
    uint64_t getFastPathF8() const { return m_fast_path_f8; }
    double getCacheHitRate() const {
        uint64_t total = m_cache_hits + m_cache_misses;
        return total > 0 ? (double)m_cache_hits / total : 0.0;
    }
};

// SIMD-optimized hot memory pool
class SIMDHotMemoryPool {
private:
    alignas(64) std::vector<uint8_t> m_hot_memory_pool;
    alignas(64) uint64_t m_hot_memory_accesses = 0;
    alignas(64) uint64_t m_simd_executions = 0;
    uint32_t m_start_address;
    uint32_t m_end_address;

public:
    SIMDHotMemoryPool(uint32_t start_addr, uint32_t end_addr) 
        : m_start_address(start_addr), m_end_address(end_addr) {
        size_t size = end_addr - start_addr;
        m_hot_memory_pool.resize(size);
        
        // Initialize with SIMD operations
        __m256i pattern = _mm256_set_epi8(0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
                                         0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
                                         0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
                                         0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00);
        
        for (size_t i = 0; i < size; i += 32) {
            if (i + 32 <= size) {
                _mm256_store_si256((__m256i*)(&m_hot_memory_pool[i]), pattern);
            }
        }
    }
    
    __forceinline bool isHotMemoryAddress(uint32_t address) const {
        return address >= m_start_address && address < m_end_address;
    }
    
    __forceinline uint8_t getHotMemoryByte(uint32_t address) {
        m_hot_memory_accesses++;
        return m_hot_memory_pool[address - m_start_address];
    }
    
    __forceinline __m256i getHotMemoryVector(uint32_t address) {
        m_simd_executions++;
        return _mm256_load_si256((__m256i*)(&m_hot_memory_pool[address - m_start_address]));
    }
    
    // Statistics
    uint64_t getHotMemoryAccesses() const { return m_hot_memory_accesses; }
    uint64_t getSimdExecutions() const { return m_simd_executions; }
};

// Memory callback types
// Memory callback types removed for now

// H8S/2350 CPU Registers (updated based on MAME H8S/2357)
struct H8S2350Registers {
    // General purpose registers
    uint32_t er[8];        // ER0-ER7 (32-bit)
    uint16_t e[8];         // E0-E7 (upper 16-bit)
    uint16_t r[8];         // R0-R7 (lower 16-bit)
    uint8_t rh[8];         // R0H-R7H (upper 8-bit)
    uint8_t rl[8];         // R0L-R7L (lower 8-bit)
    
    // Special registers
    uint32_t pc;           // Program Counter  
    uint32_t sp;           // Stack Pointer (DEPRECATED - use ER7 alias, kept for compatibility)
    uint16_t ccr;          // Condition Code Register
    uint16_t exr;          // Extended Register (added from MAME)
    
    // Control registers
    uint32_t vbr;          // Vector Base Register (24-bit in H8S, using uint32_t)
    uint16_t sbr;          // System Base Register
    uint16_t brr;          // Bus Release Register
};

// CPU Status Flags (updated based on MAME)
struct H8SFlags {
    bool carry : 1;
    bool overflow : 1;
    bool zero : 1;
    bool negative : 1;
    bool half_carry : 1;
    bool user_bit : 1;
    bool interrupt_mask : 1;
    bool trace : 1;        // Added from MAME
};

// Operating modes
enum class H8S2350Mode {
    MODE_1 = 1,    // Single-chip mode
    MODE_4 = 4,    // Expanded mode (minimum mode)
    MODE_5 = 5     // Expanded mode (maximum mode)
};

// Advanced Mode Bus Controller (8 areas, 16MB each)
struct AdvancedModeBusController {
    // Bus specification for each area (0-7)
    struct AreaConfig {
        uint16_t BCR;      // Bus Control Register
        uint16_t WCR;      // Wait Control Register
        uint16_t MCR;      // Memory Control Register
        uint16_t DCR;      // DRAM Control Register
        bool enabled;      // Area enabled
        uint32_t start;    // Start address
        uint32_t size;     // Area size
    };
    
    AreaConfig areas[8];   // 8 memory areas
    uint16_t SYSCR;        // System Control Register
    uint16_t MDCR;         // Mode Control Register
    uint16_t MSTCR;        // Master Control Register
    bool advanced_mode;    // Advanced mode enabled
};

// Interrupt types (updated based on MAME H8S/2357)
enum class H8S2350Interrupt {
    RESET = 0,
    NMI = 1,
    IRQ0 = 2,
    IRQ1 = 3,
    IRQ2 = 4,
    IRQ3 = 5,
    WOVI = 6,      // Watchdog overflow
    CMI = 7,       // Compare match
    ADI = 8,       // A/D conversion end
    IMIA0 = 9,     // Timer channel 0
    IMIB0 = 10,
    OVI0 = 11,
    IMIA1 = 12,    // Timer channel 1
    IMIB1 = 13,
    OVI1 = 14,
    // k2.txt: HEW official vector numbers for H8S/2350
    ERI0 = 80,     // SCI channel 0 receive error
    RXI0 = 81,     // SCI channel 0 receive
    TXI0 = 82,     // SCI channel 0 transmit
    TEI0 = 83,     // SCI channel 0 transmit end
    ERI1 = 84,     // SCI channel 1 receive error
    RXI1 = 85,     // SCI channel 1 receive
    TXI1 = 86,     // SCI channel 1 transmit
    TEI1 = 87,     // SCI channel 1 transmit end
    // k2.txt: H8S/2350 has no SCI2 (88-91 reserved)
    MAX_INTERRUPTS = 64
};

// H8S/2350 Emulator Class (updated based on MAME)
class H8S2350Emulator {
public:
    H8S2350Emulator();
    ~H8S2350Emulator();

    // Debug: expose last decoded instruction start/size/primary for tests
    struct DebugLast { 
        uint32_t start = 0; 
        uint8_t size = 0; 
        uint8_t primary = 0; 
        uint32_t prev = 0;  // previous instruction start PC
    };
    uint32_t dbgLastStart()   const { return m_dbg_last.start; }
    uint8_t  dbgLastSize()    const { return m_dbg_last.size; }
    uint8_t  dbgLastPrimary() const { return m_dbg_last.primary; }

    // Stable, post-execute snapshot for tests (exact instruction that just ran)
    struct LastExec {
        uint32_t start = 0;   // decoded_pc of executed instruction
        uint32_t pc    = 0;   // final PC after execute (post-clamp/fuse)
        uint8_t  size  = 0;   // instruction size (2 or 3 for these ops)
        uint8_t  primary = 0; // primary opcode
    };
    // Test getters
    uint32_t lastExecStart()   const { return m_last_exec.start; }
    uint32_t lastExecPC()      const { return m_last_exec.pc; }
    uint8_t  lastExecSize()    const { return m_last_exec.size; }
    uint8_t  lastExecPrimary() const { return m_last_exec.primary; }
    // Updater used by executor wrapper
    void updateLastExec(uint32_t start, uint8_t sz, uint32_t pc, uint8_t prim);

    // Memory access callback types
    // ReadByteCallback: returns true if handled (value set in out), false to use normal read path
    using ReadByteCallback = std::function<bool(uint32_t, uint8_t&)>;
    using WriteByteCallback = std::function<void(uint32_t, uint8_t)>;
    using ReadWordCallback = std::function<uint16_t(uint32_t)>;
    using WriteWordCallback = std::function<void(uint32_t, uint16_t)>;
    using ReadLongCallback = std::function<uint32_t(uint32_t)>;
    
    // fw8.txt Multi-SCI Panel-MP callback types
    using SciTxCallback = std::function<void(int sciId, uint8_t)>;  // Multi-SCI TDR write callback
    using SciConfigCallback = std::function<void(int sciId, bool txEnabled, bool syncMode, uint32_t baud)>;  // SCI config callback
    using WriteLongCallback = std::function<void(uint32_t, uint32_t)>;

    // Setters for memory callbacks
    void setReadByteCallback(ReadByteCallback cb) { m_read_byte_cb = std::move(cb); }
    void setWriteByteCallback(WriteByteCallback cb) { m_write_byte_cb = std::move(cb); }
    void setReadWordCallback(ReadWordCallback cb) { m_read_word_cb = std::move(cb); }
    void setWriteWordCallback(WriteWordCallback cb) { m_write_word_cb = std::move(cb); }
    void setReadLongCallback(ReadLongCallback cb) { m_read_long_cb = std::move(cb); }
    void setWriteLongCallback(WriteLongCallback cb) { m_write_long_cb = std::move(cb); }
    
    // fw8.txt Multi-SCI Panel-MP hooks
    void setSciTxCallback(SciTxCallback cb) { m_sci_tx_cb = std::move(cb); }
    void setSciConfigCallback(SciConfigCallback cb) { m_sci_config_cb = std::move(cb); }
    
    // i15.txt MIDI IN/OUT - SCI RX injection for MIDI→FW pathway
    void sciInjectRxByte(int channel, uint8_t data);
    
    // i17.txt MIDI smoke gating
    bool midiSmokeReady(int selectedSciChannel = 0) const;
    
    // i16.txt Minimum Viable IRQ system
    void irqRaise(int vec);
    void irqClear(int vec);
    void irqTryService();
    bool cpuInterruptsEnabled() const;
    // BUG75: the IPR level of one interrupt vector, 0-7, per Table 5.4 (rendered
    // pages 102-106 region) read against Table 5.3 (rendered page 131). NMI returns 8.
    uint8_t irqSourceLevel(int vec) const;
    bool isIrqInService() const { return m_irq_in_service; }
    void clearIrqInService() { m_irq_in_service = false; }
    
    // k4.txt: Assisted-VBR control
    void setAssistedVBR(bool enabled) { m_assist_vbr_enabled = enabled; }
    
    // k5.txt: Dev illegal guard control  
    void setDevIllegalGuard(bool enabled) { m_dev_illegal_guard_enabled = enabled; }
    
    // IRQ control for stack corruption debugging
    void setIRQMask(bool disabled) {
        if (disabled) {
            m_registers.exr |= 0x80;   // I=1 → IRQ off
            printf("[IRQ] interrupts DISABLED (EXR=0x%04X) - stack debugging mode\n", m_registers.exr);
        } else {
            m_registers.exr &= ~0x80;  // I=0 → IRQ on  
            printf("[IRQ] interrupts ENABLED (EXR=0x%04X) - normal operation\n", m_registers.exr);
        }
    }

    // Core CPU functions
    void reset();
    void step();
    void execute(uint32_t cycles);
    
    // Register access
    H8S2350Registers& getRegisters() { return m_registers; }
    const H8S2350Registers& getRegisters() const { return m_registers; }
    H8SFlags& getFlags() { return m_flags; }
    const H8SFlags& getFlags() const { return m_flags; }
    
    // Memory access (updated based on MAME)
    // PERF-MCU-11 (2026-10-01): the instruction-byte window (PERF-MCU-1) inline - executors re-read their own bytes
    // constantly and the out-of-line readByte() paid a large frame for it (10 % of the MCU profile, most of it here).
    uint8_t readByte(uint32_t address) {
        if (m_insnRawOn) {
            const uint32_t d = address - m_busPc0;
            if (d < m_busInsnSize) return m_insnRaw[d];
        }
        return readByteBus(address);
    }
    uint8_t readByteBus(uint32_t address);   // everything else: the bus path, unchanged
    // AUDIT-3 (tools/diffref site mode): the last instruction's off-chip data accesses, as BUS-DATA-STATES recorded them.
    uint32_t lastInsnBusCount() const { return m_busRecLastN; }   // == 16 (kBusRecMax) means "maybe more"
    void     clearLastInsnBus() { m_busRecLastN = 0; }   // call before step(): the no-data fast path leaves it alone
    uint32_t lastInsnBusAddr(uint32_t i) const { return i < m_busRecLastN ? m_busRec[i].a : 0xFFFFFFFFu; }
    uint16_t readWord(uint32_t address);
    uint32_t readLong(uint32_t address);
    void writeByte(uint32_t address, uint8_t value);
    void writeWord(uint32_t address, uint16_t value);
    void writeLong(uint32_t address, uint32_t value);
    // BUG126 (2026-09-26): the instruction executor's word/longword STORES. CS0 (the x16 flash,
    // 0x000000-0x0FFFFF) takes a MOV.W as ONE bus cycle (writeWord -> flashBusCycle width 2), so
    // the MBM29LV800B command sequences (AA/55/80/AA/55/30, AA/55/A0/data) reach it intact;
    // everywhere else the store stays exactly what it was: bytes, high byte first.
    uint64_t m_tickedCycles = 0;   // BUG126 measurement: sum of tickPeripherals() cycles
    void storeWord(uint32_t address, uint16_t value) {
        if ((address & 0x00FFFFFFu) < 0x100000u) { writeWord(address, value); return; }
        writeByte(address, uint8_t(value >> 8));
        writeByte(address + 1, uint8_t(value));
    }
    void storeLong(uint32_t address, uint32_t value) {
        storeWord(address, uint16_t(value >> 16));
        storeWord(address + 2, uint16_t(value));
    }
    
    // VBR and TRAPA configuration
    void setVBR(uint32_t vbr) { 
        m_registers.vbr = vbr & 0x00FFFFFF;  // H8S VBR is 24-bit 
        if (m_trap_cfg.autoDetect && !m_trap_cfg.locked) {
            autodetectTrapBase();
        }
        if (m_debug_mode) {
            std::cout << "[VBR] Set to 0x" << std::hex << vbr << std::dec << std::endl;
        }
    }
    uint32_t getVBR() const { return m_registers.vbr & 0x00FFFFFF; }
    
    // H8S/2350 manual-compliant TRAPA execution (0x57 xx)
    bool executeTrap(uint8_t imm8);
    // Bit Manipulation Groups (0x7C/7D/7E/7F) - H8S/2600 Manual
    bool executeBitManipGroup(uint8_t primaryOpcode, uint8_t& instruction_size);
    void setTrapConfig(const TrapVectorConfig& cfg) {
        // Respect locked configuration - only allow changes if not locked
        if (!m_trap_cfg.locked) {
            m_trap_cfg = cfg;
        } else if (m_debug_mode) {
            std::cout << "[TRAPA] Configuration locked (base=" << (int)m_trap_cfg.baseIndex
                      << "), ignoring setTrapConfig call" << std::dec << std::endl;
        }
    }
    TrapVectorConfig getTrapConfig() const { return m_trap_cfg; }
    
    // VBR Control Register Write Detection (from readme.nfo implementation plan)
    void writeControlReg_VBR(uint32_t val);
    
    // Legacy memory access functions (for compatibility)
    uint8_t readMemory8(uint32_t address) { return readByte(address); }
    uint16_t readMemory16(uint32_t address) { return readWord(address); }
    uint32_t readMemory32(uint32_t address) { return readLong(address); }
    void writeMemory8(uint32_t address, uint8_t value) { writeByte(address, value); }
    void writeMemory16(uint32_t address, uint16_t value) { writeWord(address, value); }
    void writeMemory32(uint32_t address, uint32_t value) { writeLong(address, value); }
    
    // I/O Register access (updated based on MAME)
    uint8_t readIORegister(uint32_t address);
    void writeIORegister(uint32_t address, uint8_t value);
    uint8_t readIORegisterStruct(uint32_t address);
    void writeIORegisterStruct(uint32_t address, uint8_t value);
    
    
    // Firmware loading
    bool loadFirmware(const std::vector<uint8_t>& firmware);
    bool loadFirmwareFromFile(const std::string& filename);
    
    // Peripheral access
    void setPeripheralEmulator(std::shared_ptr<H8S2350PeripheralEmulator> peripheral);
    std::shared_ptr<H8S2350PeripheralEmulator> getPeripheralEmulator() const { return m_peripheral; }
    
    // Interrupt system
    void setInterruptSystem(std::shared_ptr<H8S2350InterruptSystem> intc);
    std::shared_ptr<H8S2350InterruptSystem> getInterruptSystem() const { return m_intc; }
    
    // System Control Register (added from MAME)
    uint8_t readSYSCR() const { return m_syscr; }
    void writeSYSCR(uint8_t value);
    
    // Cycle counting API
    inline uint64_t getCycles() const { return m_cycles; }
    inline void     resetCycles()     { m_cycles = 0; }
    inline void     addCycles(uint32_t c) { m_cycles += c; } // hot-path inline
    
    // Debug functions
    void setDebugMode(bool enabled) { m_debug_mode = enabled; }
    bool isDebugMode() const { return m_debug_mode; }

    // lcd5.txt: CPU trace control to prevent muting after I/O scan
    void setTraceCpuAlways(bool enabled) { m_trace_cpu_always = enabled; }
    bool isTraceCpuAlways() const { return m_trace_cpu_always; }

    // FIX21: Quiet boot - suppress per-instruction verbose trace for fast boot
    void setQuietBoot(bool enabled) { m_quietBoot = enabled; }
    bool isQuietBoot() const { return m_quietBoot; }

    // Verification functions
    uint64_t getRealInstructionCount() const { return m_real_instruction_count; }
    uint64_t getStubInstructionCount() const { return m_stub_instruction_count; }
    uint64_t getTotalInstructionCount() const { return m_total_instruction_count; }
    double getRealInstructionRatio() const { 
        return m_total_instruction_count > 0 ? 
               (double)m_real_instruction_count / m_total_instruction_count : 0.0; 
    }
    void printVerificationStats() const;
    
    // State management
    void saveState(const std::string& filename);
    bool loadState(const std::string& filename);
    
    // Legacy functions (for compatibility)
    bool initialize() { reset(); return true; }
    void run(uint32_t cycles) { execute(cycles); }
    void halt() { m_halted = true; }
    void resume() { m_halted = false; }
    bool isRunning() const { return !m_halted; }
    bool isHalted() const { return m_halted; }
    uint32_t getExecutedCycles() const { return m_cycles_executed; }      // Legacy 32-bit counter
    uint32_t getInstructionCount() const { return m_cycles_executed; }  // Added for compatibility
    H8S2350Mode getMode() const { return m_mode; }
    
    // Interrupt functions (for compatibility)
    void triggerInterrupt(H8S2350Interrupt interrupt);
    void enableInterrupt(H8S2350Interrupt interrupt);
    void disableInterrupt(H8S2350Interrupt interrupt);
    void enableInterrupts() { /* TODO: Implement */ }
    void disableInterrupts() { /* TODO: Implement */ }
    void setInterruptPriority(H8S2350Interrupt interrupt, int priority);
    
    // Program counter control
    void setProgramCounter(uint32_t pc) {
#if defined(DEBUG) && defined(TRACE_PC_WRITES)
        if (m_pc_write_watch_active) {
            printf("[PCWRITE-LATE] pc<=%06X tag=%s\n", pcMask24(pc), m_pc_write_watch_tag.c_str());
        }
#endif
#if defined(_DEBUG) || defined(H8S_ENFORCE_SHIFTROT_PC)
        // PC fuse: block any late writes while fuse is active
        if (m_pc_fuse.active) {
            const uint32_t want = m_pc_fuse.want & 0x00FFFFFFu;
            const uint32_t in   = pcMask24(pc);
            if (in != want) {
                printf("[PCFUSE] late write blocked (op=%02X) had=%06X -> forcing=%06X\n",
                       (unsigned)m_pc_fuse.op, in, want);
                pc = want;
            }
        }
#endif
        m_registers.pc = pcMask24(pc);
    }
    uint32_t getProgramCounter() const { return m_registers.pc; }
    
    // ER7 ↔ SP alias control (H8S architecture requirement)
    inline uint32_t getSP24() const { 
        return pcMask24(m_registers.er[7]); 
    }
    
    inline void setSP24(uint32_t sp) {
        uint32_t old_sp = m_registers.sp;
        m_registers.er[7] = (m_registers.er[7] & 0xFF000000) | pcMask24(sp);
        m_registers.sp = pcMask24(sp);  // Keep compatibility field synced
        // BUG121 (DIFFREF): BSR/JSR/RTS/PUSH/POP came through here and left E7/R7/R7H/R7L
        // stale - the next byte or word write to R7 would rebuild ER7 from them.
        m_registers.e[7]  = uint16_t(m_registers.er[7] >> 16);
        m_registers.r[7]  = uint16_t(m_registers.er[7]);
        m_registers.rh[7] = uint8_t(m_registers.er[7] >> 8);
        m_registers.rl[7] = uint8_t(m_registers.er[7]);
        traceSPModification(old_sp, m_registers.sp, "setSP24");
    }
    
    // Register synchronization helpers
    inline void syncRegAfterByteWrite(int d, bool is_high) {
        if (d < 0 || d >= 8) {
            // BUG107: this return was SILENT, and a caller passing the 4-bit register field
            // (8..15 = R0L..R7L) lost every byte load into an RnL from the r[]/er[] views.
            // A silent refusal is a hole: say so, once per index.
            static bool told[64] = {};
            const int k = (d < 0 ? 0 : d) & 63;
            if (!told[k]) { told[k] = true;
                printf("[SYNC-INDEX] syncRegAfterByteWrite(%d, %s) - index outside 0..7, the byte "
                       "write did NOT reach r[]/er[] (PC=0x%06X)\n", d, is_high ? "high" : "low",
                       m_effectivePC); }
            return;
        }
        uint32_t old_sp = (d == 7) ? m_registers.sp : 0;
        if (is_high) {
            m_registers.r[d] = (m_registers.rh[d] << 8) | (m_registers.r[d] & 0x00FF);
        } else {
            m_registers.r[d] = (m_registers.r[d] & 0xFF00) | m_registers.rl[d];
        }
        m_registers.er[d] = (m_registers.er[d] & 0xFFFF0000) | m_registers.r[d];
        if (d == 7) {
            m_registers.sp = pcMask24(m_registers.er[7]);
            traceSPModification(old_sp, m_registers.sp, "syncRegAfterByteWrite(7)");
        }
        // CHATGPT PROTOCOL: SP/R7 dirty write detection
        if (d == 7) {
            m_r7_modified_this_cycle = true;
            m_sp_modified_this_cycle = true;
        }
    }
    
    inline void syncRegAfterWordWrite(int d) {
        if (d < 0 || d >= 8) {   // BUG107: loud, not silent (see syncRegAfterByteWrite)
            static bool told = false;
            if (!told) { told = true;
                printf("[SYNC-INDEX] syncRegAfterWordWrite(%d) - index outside 0..7, the word write "
                       "did NOT reach er[] (PC=0x%06X)\n", d, m_effectivePC); }
            return;
        }
        uint32_t old_sp = (d == 7) ? m_registers.sp : 0;
        m_registers.rl[d] = m_registers.r[d] & 0xFF;
        m_registers.rh[d] = (m_registers.r[d] >> 8) & 0xFF;
        m_registers.er[d] = (m_registers.er[d] & 0xFFFF0000) | m_registers.r[d];
        if (d == 7) {
            m_registers.sp = pcMask24(m_registers.er[7]);
            traceSPModification(old_sp, m_registers.sp, "syncRegAfterWordWrite(7)");
        }
        // CHATGPT PROTOCOL: SP/R7 dirty write detection
        if (d == 7) {
            m_r7_modified_this_cycle = true;
            m_sp_modified_this_cycle = true;
        }
    }
    
    inline void syncRegAfterLongWrite(int d) {
        if (d < 0 || d >= 8) return;
        uint32_t old_sp = (d == 7) ? m_registers.sp : 0;
        m_registers.e[d] = (m_registers.er[d] >> 16) & 0xFFFF;
        m_registers.r[d] = m_registers.er[d] & 0xFFFF;
        m_registers.rl[d] = m_registers.r[d] & 0xFF;
        m_registers.rh[d] = (m_registers.r[d] >> 8) & 0xFF;
        if (d == 7) {
            m_registers.sp = pcMask24(m_registers.er[7]);
            traceSPModification(old_sp, m_registers.sp, "syncRegAfterLongWrite(7)");
        }
        // CHATGPT PROTOCOL: SP/R7 dirty write detection
        if (d == 7) {
            m_r7_modified_this_cycle = true;
            m_sp_modified_this_cycle = true;
        }
    }
    
    inline void syncRegAfterUpperWordWrite(int d) {
        if (d < 0 || d >= 8) return;
        uint32_t old_sp = (d == 7) ? m_registers.sp : 0;
        m_registers.er[d] = (static_cast<uint32_t>(m_registers.e[d]) << 16) | (m_registers.er[d] & 0x0000FFFF);
        if (d == 7) {
            m_registers.sp = pcMask24(m_registers.er[7]);
            traceSPModification(old_sp, m_registers.sp, "syncRegAfterUpperWordWrite(7)");
        }
        // CHATGPT PROTOCOL: SP/R7 dirty write detection
        if (d == 7) {
            m_r7_modified_this_cycle = true;
            m_sp_modified_this_cycle = true;
        }
    }

    // Safe ERd write with ER7-SP alias handling
    inline void setERd(int d, uint32_t val) {
        if (d < 0 || d >= 8) return;  // Bounds check
        m_registers.er[d] = val;
        syncRegAfterLongWrite(d);
    }
    

    
    // Peripheral access (for compatibility)
    H8S2350PeripheralEmulator* getPeripherals() { return m_peripheral.get(); }
    
    // Memory dump (for compatibility)
    std::vector<uint8_t> dumpMemory(uint32_t start, uint32_t size);
    
    // Stack operations (for compatibility)
    void pushToStack(uint32_t value);
    void pushExceptionFrame(uint32_t pc, uint8_t ccr, uint8_t exr);
    uint32_t popFromStack();
    
    // 24-bit stack operations for JSR/RTS (H8S/2350 advanced mode)
    void push24(uint32_t value);
    uint32_t pop24();
    
    // 8-bit stack operations for CCR/EXR (H8S/2350 advanced mode)  
    void pushByte(uint8_t value);
    uint8_t popByte();
    
    // CCR restoration from stack byte (all bits including I and UI)
    void setCCRFromByte(uint8_t ccr_byte);
    // BUG97: the live CCR byte. m_registers.ccr is a SHADOW - the ALU writes
    // m_flags and only instructions that touch CCR explicitly write the shadow.
    // Anything that STACKS a CCR must use this, never m_registers.ccr.
    uint8_t ccrByteLive() const;
    
    // H8S/2350 manual-compliant RTE execution
    bool executeRTE();
    
    // Taint-tracked PC stack operations
    void pushPC24(uint32_t pcValue);
    uint32_t popPC24();

    // Stack alignment and safety checks
    void SP_ASSERT_EVEN() const;
    bool isStackOverflow(uint32_t decrement = 0) const;
    bool isStackUnderflow(uint32_t increment = 0) const;

    // Stack alignment methods (v2.txt improvement #2)
    void assertStackEven();
    void enforceStackEven();
    bool isStackWordAligned() const;
    void getStackAlignmentStats(uint32_t& errors, uint32_t& corrections) const;
    void resetStackAlignmentStats();
    void pushWordAligned(uint16_t value);
    void pushLongAligned(uint32_t value);
    uint16_t popWordAligned();
    uint32_t popLongAligned();
    
    // Get I/O trace log
    const std::vector<std::string>& getIOTraceLog() const { return m_io_trace_log; }
    void clearIOTraceLog() { m_io_trace_log.clear(); }
    
    // Memory callbacks (for external memory system)
    ReadByteCallback m_read_byte_cb = nullptr;
    WriteByteCallback m_write_byte_cb = nullptr;
    ReadWordCallback m_read_word_cb = nullptr;
    WriteWordCallback m_write_word_cb = nullptr;
    ReadLongCallback m_read_long_cb = nullptr;
    WriteLongCallback m_write_long_cb = nullptr;
    
    // fw8.txt Multi-SCI Panel-MP callbacks
    SciTxCallback m_sci_tx_cb = nullptr;
    SciConfigCallback m_sci_config_cb = nullptr;
    
    // i16.txt Minimum Viable IRQ system
    static constexpr int MAX_VEC = 256;
    std::bitset<MAX_VEC> m_irq_pending;
    uint64_t m_irqWords[4] = {};      // PERF-MCU-13: the same bits as m_irq_pending, as words (irqTryService scans set bits only)
    uint32_t m_irqPendingCount = 0;   // PERF-MCU-10: == m_irq_pending.count(); irqRaise/irqClear are the only writers (bitset::any() looped over 4 words, 2.4 % of the MCU profile)
    bool m_irq_in_service = false;
    uint32_t m_tpu2_tick_accum = 0;   // TPU2 TGI2A periodic-tick cycle accumulator (timer IRQ)
    bool m_tpu2_started = false;      // set when firmware writes TSTR.CST2 (0xFFFFC0 bit2)
    uint32_t m_last_exception_frame_sp = 0;  // SP after pushExceptionFrame (frame base), for CCR patching
    
    // Debug functions (for compatibility)
    void setBreakpoint(uint32_t address);
    
    // MS2000 specific access
    MS2000MPStub* getMPStub() { return m_mp_stub.get(); }
    MS2000PanelADC* getPanelADC() { return m_panel_adc.get(); }
    MS2000SwitchMatrix* getSwitchMatrix() { return m_switch_matrix.get(); }
    MS2000LEDMatrix* getLEDMatrix() { return m_led_matrix.get(); }
    MS2000MIDIInterfaceSimple* getMIDIInterface() { return m_midi_interface.get(); }
    void clearBreakpoint(uint32_t address);
    std::string getDisassembly(uint32_t address, size_t count = 1);
    void dumpRegisters();
    void dumpMemoryMap();
    
    // MS2000 specific functions (for compatibility)
    void updateSwitchMatrix();
    void updateLEDMatrix();
    void updatePotentiometers();
    void updateLCDDisplay();
    void processMIDI();
    
    
    // SIMD Optimization Functions
    void enableSIMDOptimizations(const SIMDOptimizationConfig& config);
    void disableSIMDOptimizations();
    bool isSIMDEnabled() const { return m_simd_config.enabled; }
    const SIMDOptimizationConfig& getSIMDConfig() const { return m_simd_config; }
    
    // SIMD-optimized memory access
    __forceinline uint8_t readByteSIMD(uint32_t address) {
        if (m_simd_config.enabled && m_simd_config.hot_memory_enabled && m_simd_hot_memory) {
            if (m_simd_hot_memory->isHotMemoryAddress(address)) {
                return m_simd_hot_memory->getHotMemoryByte(address);
            }
        }
        return readByte(address);
    }
    
    __forceinline uint16_t readWordSIMD(uint32_t address) {
        if (m_simd_config.enabled && m_simd_config.hot_memory_enabled && m_simd_hot_memory) {
            if (m_simd_hot_memory->isHotMemoryAddress(address) && 
                m_simd_hot_memory->isHotMemoryAddress(address + 1)) {
                // Read two bytes from hot memory
                uint8_t low = m_simd_hot_memory->getHotMemoryByte(address);
                uint8_t high = m_simd_hot_memory->getHotMemoryByte(address + 1);
                return (high << 8) | low;
            }
        }
        return readWord(address);
    }
    
    __forceinline uint32_t readLongSIMD(uint32_t address) {
        if (m_simd_config.enabled && m_simd_config.hot_memory_enabled && m_simd_hot_memory) {
            if (m_simd_hot_memory->isHotMemoryAddress(address) && 
                m_simd_hot_memory->isHotMemoryAddress(address + 1) &&
                m_simd_hot_memory->isHotMemoryAddress(address + 2) &&
                m_simd_hot_memory->isHotMemoryAddress(address + 3)) {
                // Read four bytes from hot memory
                uint8_t b0 = m_simd_hot_memory->getHotMemoryByte(address);
                uint8_t b1 = m_simd_hot_memory->getHotMemoryByte(address + 1);
                uint8_t b2 = m_simd_hot_memory->getHotMemoryByte(address + 2);
                uint8_t b3 = m_simd_hot_memory->getHotMemoryByte(address + 3);
                return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
            }
        }
        return readLong(address);
    }
    
    // SIMD-optimized instruction execution
    void executeSIMDBatch(uint32_t cycles);
    void executeVectorizedInstructions(uint32_t start_pc, uint32_t count);
    
    // Internal SIMD processing
    void processSIMDInstructions(const uint8_t* instructions, uint32_t base_pc);
    void processEnhancedSIMDInstructions(const uint8_t* instructions, const uint32_t* pc_offsets, uint8_t* opcode_types);
    
    // SIMD statistics
    uint64_t getSIMDCacheHits() const;
    uint64_t getSIMDCacheMisses() const;
    uint64_t getSIMDFastPath6b() const;
    uint64_t getSIMDFastPath5e() const;
    uint64_t getSIMDFastPath1b() const;
    uint64_t getSIMDFastPathF8() const;
    uint64_t getSIMDHotMemoryAccesses() const;
    uint64_t getSIMDExecutions() const;
    double getSIMDCacheHitRate() const;
    
    // Timer and Interrupt System
    void initializeTimer();
    void resetTimer();
    void updateTimer(uint32_t cycles);
    void handleTimerInterrupt();
    void enableInterrupt(uint8_t interrupt_number);
    void disableInterrupt(uint8_t interrupt_number);
    void triggerInterrupt(uint8_t interrupt_number);
    void handleInterrupts();
    bool hasPendingInterrupt() const;
    
    // TPU (Timer Pulse Unit) Functions
    void initializeTPU();
    void resetTPU();
    void updateTPU(uint32_t cycles);
    void handleTPUInterrupt(uint8_t channel);
    
    // PPG (Programmable Pulse Generator) Functions
    void initializePPG();
    void resetPPG();
    void updatePPG(uint32_t cycles);
    
    // Watchdog Timer Functions
    void initializeWatchdog();
    void resetWatchdog();
    void updateWatchdog(uint32_t cycles);
    void feedWatchdog();
    
    // SCI (Serial Communication Interface) Functions
    void initializeSCI();
    void resetSCI();
    void updateSCI(uint32_t cycles);
    void sendSCI(uint8_t channel, uint8_t data);
    uint8_t receiveSCI(uint8_t channel);
    
    // A/D Converter Functions
    void initializeADC();
    void resetADC();
    void updateADC(uint32_t cycles);
    void startADCConversion(uint8_t channel);
    uint16_t readADCChannel(uint8_t channel);
    void setADCInput(uint8_t channel, uint16_t value);
    
    // D/A Converter Functions
    void initializeDAC();
    void resetDAC();
    void updateDAC(uint32_t cycles);
    void writeDACChannel(uint8_t channel, uint8_t value);
    uint8_t readDACChannel(uint8_t channel);
    
    // DMA Controller Functions
    void initializeDMAC();
    void resetDMAC();
    void updateDMAC(uint32_t cycles);
    void startDMATransfer(uint8_t channel, uint32_t source, uint32_t dest, uint16_t count);
    void stopDMATransfer(uint8_t channel);
    bool isDMATransferActive(uint8_t channel) const;
    void setDMAMode(uint8_t channel, bool full_address_mode);
    
    // Data Transfer Controller Functions
    void initializeDTC();
    void resetDTC();
    void updateDTC(uint32_t cycles);
    void startDTCTransfer(uint8_t channel, uint32_t source, uint32_t dest, uint16_t count);
    void stopDTCTransfer(uint8_t channel);
    bool isDTCTransferActive(uint8_t channel) const;
    void triggerDTCByInterrupt(uint8_t interrupt_source);
    
    // I/O Register Functions
    void initializeIORegisters();
    void resetIORegisters();
    void handlePortChange(uint8_t port, uint8_t old_value, uint8_t new_value);
    
    // *** FIRMWARE COMMUNICATION FIX: LCD Communication Functions ***
    void handleLCDCommunication(uint32_t address, uint8_t old_value, uint8_t new_value);
    void processLCDCommand(uint8_t command);
    void processLCDData(uint8_t data);
    
    // LCD Display connection
    void setLCDDisplay(RealLCDDisplay* lcd_display);
    
    // Panel Interface connection
    void setPanelInterface(PanelIF* panel_interface);
    
    // Advanced Mode Functions
    void initializeAdvancedMode();
    void resetAdvancedMode();
    void configureArea(uint8_t area, uint32_t start, uint32_t size, uint16_t bcr, uint16_t wcr);
    bool isAdvancedModeEnabled() const;
    uint8_t getAreaForAddress(uint32_t address) const;
    bool isAddressInArea(uint32_t address, uint8_t area) const;
    uint32_t translateAdvancedModeAddress(uint32_t address) const;
    
    // Clock system functions
    void setClockFrequency(uint32_t frequency_hz);
    void setHighSpeedMode(bool enabled);
    uint32_t getClockFrequency() const { return m_clock_frequency; }
    bool isHighSpeedMode() const { return m_high_speed_mode; }
    void updateClockSystem();
    
    
    // Memory mapping
    bool isFlashAddress(uint32_t address) const;
    bool isRAMAddress(uint32_t address) const;
    bool isIOAddress(uint32_t address) const;
    bool isExternalMemoryAddress(uint32_t address) const;
    
    // Advanced mode memory area functions
    bool isAdvancedModeArea(uint32_t address, uint8_t& area) const;
    uint32_t getAdvancedModeAreaStart(uint8_t area) const;
    uint32_t getAdvancedModeAreaSize(uint8_t area) const;
    
    // Instruction handlers (for compatibility)
    void handleMoveByte(uint8_t operand);
    void handleMoveWord(uint8_t operand);
    void handleMoveLong(uint8_t operand);
    void handleAdd(uint8_t operand);
    void handleSubtract(uint8_t operand);
    void handleCompare(uint8_t operand);
    void handleAnd(uint8_t operand);
    void handleOr(uint8_t operand);
    void handleXor(uint8_t operand);
    void handleNot(uint8_t operand);
    void handleShiftLeft(uint8_t operand);
    void handleShiftRight(uint8_t operand);
    void handleRotateLeft(uint8_t operand);
    void handleRotateRight(uint8_t operand);
    void handleJump(uint8_t operand);
    void handleJumpSubroutine(uint8_t operand);
    void handleBranchAlways(uint8_t operand);
    void handleBranch(uint8_t operand);
    void handlePush(uint8_t operand);
    void handlePop(uint8_t operand);
    void handleIncrement(uint8_t operand);
    void handleDecrement(uint8_t operand);
    void handleClear(uint8_t operand);
    void handleTest(uint8_t operand);
    void handleNegate(uint8_t operand);
    void handleExtend(uint8_t operand);
    void handleSwap(uint8_t operand);
    void handleExchange(uint8_t operand);
    void handleLink(uint8_t operand);
    void handleUnlink(uint8_t operand);
    void handleTrap(uint8_t operand);
    void handleIllegalInstruction(); // k1.txt: Illegal instruction exception (vector 4)
    
    // k8.txt: VBR promotion helper
    bool tryPromoteVBR();

    // fw4.txt: SCI kick-start mechanism
    void checkKickStart();

    // LCD Callback setters
    void setLCDWriteCallback(LCDWriteCallback callback) { m_lcd_write_callback = callback; }
    void setLCDReadCallback(LCDReadCallback callback) { m_lcd_read_callback = callback; }
    
    // MS2000 GPIO LCD adapter setup (h8s.txt implementation)
        void setupGPIOLcdAdapter(std::function<void(uint8_t)> onCmd, std::function<void(uint8_t)> onData, std::function<uint8_t(bool)> onRead, std::function<void(const char*)> log = nullptr);
    Ms2kLcdAdapter* getGPIOLcdAdapter() const { return m_gpio_lcd_adapter.get(); }
    
    // TRAPA→handler→RTE round-trip test implementation
    bool executeTrapRteRoundTripTest();
    
    // H8S/2350 Stack & Interrupt Test Suite
    bool runEr7SpAliasTest();
    bool runNestedTrapaTest();
    bool runStackBoundaryTest();
    bool runVbrRelocationTest();
    bool runIrqMaskTest();
    bool run0x7ASanityTest();
    bool runFullTestSuite();  // Runs all 6 tests with summary

    // VBR address translation (24-bit logical → physical for system space)
    // H8S/2350 belső RAM logikai ablak: 0x00F80000..0x00F81FFF -> fizikai: 0xFFF80000..0xFFF81FFF
    inline uint32_t vbrPhys(uint32_t a24) const {
        uint32_t a = a24 & 0x00FFFFFF;
        if (a >= 0x00F80000 && a < 0x00F82000) {
            return 0xFF000000u | a;   // belső RAM tükrözés
        }
        // A ROM-mirror (0x000000..0x0007FF) és egyéb 24 bites területhez itt nincs extra prefix
        return a;
    }

    // Stack address translation (24-bit logical → physical H8S RAM)
    inline uint32_t stackPhys(uint32_t sp24) const {
        uint32_t a24 = sp24 & 0x00FFFFFF;     // SP always 24-bit
        // The H8S/2350 on-chip RAM in Advanced Mode lives at 0x00FFF400-0x00FFFBFF
        // (per Renesas HEW 2350.dat). The emulator's m_ram buffer is reached through the
        // 0x00F80000-0x00F81FFF window (readByte/writeByte map that to m_ram[addr-0xF80000]).
        // Map the real hardware stack range onto that same window so JSR push and RTS pop
        // hit the SAME physical bytes. Offset within RAM = (a24 - 0xFFF400).
        if (a24 >= 0x00FFF400 && a24 <= 0x00FFFFFF) {
            return 0x00F80000u + (a24 - 0x00FFF400u);
        }
        // Legacy bootstrap stack page: 0x00F8_xxxx logical -> 0xFFF8_xxxx physical
        if (a24 >= 0x00F80000 && a24 < 0x00F82000) {
            return 0xFF000000u | a24;
        }
        // Default: pass through 24-bit address unchanged.
        return a24;
    }

    // Helper function for reading stacked 24-bit PC from TRAPA stack frame
    // This is critical for nested TRAPA test - reads PC from correct stack offset
    uint32_t readStackedPC24(uint32_t sp_after_push) const;
    
    // Peek stacked PC from current SP without modifying stack
    // TRAPA stack layout: [SP+4]=PC_MSB, [SP+3]=PC_MID, [SP+2]=PC_LSB, [SP+1]=CCR, [SP+0]=EXR
    uint32_t peekStackedPC24() const;
    
    // Memory access cycle penalties
    uint32_t memReadPenalty(uint32_t addr) const;
    uint32_t memWritePenalty(uint32_t addr) const;

private:
    // Cycle counting
    uint64_t m_cycles = 0;
    
    // TRAPA helpers
    uint32_t resolveTrapVector(uint8_t imm);
    void autodetectTrapBase();
    bool plausibleCodeAddr(uint32_t a) const;
    
    // CHATGPT PROTOCOL: First-Fault Trace Buffer (root cause tracing)
    struct TraceEntry {
        uint32_t pc;              // PC of executed instruction
        uint32_t sp_before;       // SP before instruction
        uint32_t sp_after;        // SP after instruction
        uint32_t r7_before;       // R7/ER7 before instruction (SP alias)
        uint32_t r7_after;        // R7/ER7 after instruction
        uint32_t mem_write_addr;  // Memory write address (if any)
        uint32_t mem_write_value; // Memory write value (if any)
        uint8_t  write_size;      // Size of memory write (1/2/4 bytes)
        bool writes_r7;           // Did instruction write R7/ER7?
        bool writes_sp;           // Did instruction write SP?
        bool writes_stack;        // Did instruction write to stack range?
        uint8_t opcode;           // Primary opcode
        uint8_t size;             // Instruction size
        uint64_t cycle;           // Global cycle counter
    };
    static constexpr int TRACE_BUFFER_SIZE = 32768;  // 32K entries (~1-2M instructions)
    TraceEntry m_trace_buffer[TRACE_BUFFER_SIZE];
    int m_trace_pos = 0;
    bool m_trace_enabled = true;
    bool m_first_fault_detected = false;
    int m_first_fault_trace_idx = -1;
    
    // Stack frame expectation model
    struct StackFrameExpectation {
        uint32_t expected_sp_after_call;  // Expected SP after CALL
        uint32_t expected_return_pc;      // Expected return address
        uint32_t call_pc;                 // PC of CALL instruction
        uint8_t  call_size;               // CALL instruction size
    };
    static constexpr int CALL_STACK_DEPTH = 128;
    StackFrameExpectation m_call_stack[CALL_STACK_DEPTH];
    int m_call_stack_depth = 0;
    
    // SP/R7 dirty write detection
    bool m_sp_modified_this_cycle = false;
    bool m_r7_modified_this_cycle = false;
    
    // First-fault detection
    void recordTraceEntry(const TraceEntry& entry);
    void checkFirstFaultConditions(const TraceEntry& entry);
    void dumpFirstFaultTrace();
    void validateStackFrameExpectations();

    // Call stack integrity tracking
    bool m_call_stack_ok = true;

    // CHATGPT BOOT CHECKLIST: Boot sequence observability
    enum class BootPhase {
        RESET_ENTRY,           // PC at reset vector, SP initializing
        STACK_INIT,            // SP stabilizing, early init
        EARLY_INTERRUPTS,      // First IRQs firing, CCR I-bit cleared
        MEMORY_MAP_STABLE,     // RAM remap done, execute-from-RAM
        PERIPHERAL_INIT,       // LCD, DAC, MIDI, timers init
        IDLE_STABLE            // SP stable, PC in stable loop, LCD ready
    };
    
    struct BootEvent {
        uint64_t cycle;
        uint32_t pc;
        uint32_t sp;
        uint32_t vbr;
        uint8_t ccr_i;        // CCR I-bit (interrupt enable)
        BootPhase phase;
        const char* description;
    };
    
    static constexpr int BOOT_LOG_SIZE = 4096;
    BootEvent m_boot_log[BOOT_LOG_SIZE];
    int m_boot_log_pos = 0;
    BootPhase m_current_boot_phase = BootPhase::RESET_ENTRY;
    bool m_boot_logging_enabled = true;
    
    void logBootEvent(BootPhase phase, const char* desc);
    void updateBootPhase();
    void detectFirstStableIdle();
    
    // Last memory write tracking (for trace)
    struct LastStore {
        uint32_t addr = 0;
        uint32_t value = 0;
        uint8_t size = 0;
        bool valid = false;
    };
    LastStore m_last_store;
    void updateLastStore(uint32_t addr, uint8_t size, uint32_t value);

    // Stack corruption detection (gyökérok debug)
    std::vector<uint32_t> m_shadowCallStack;  // PC_after_call lista
    int m_callDepth = 0;
    bool m_strictStack = true; // H8S_DEV_FAILSAFE
    
    struct StoreLog { 
        uint32_t addr; 
        uint8_t size; 
        uint8_t b0, b1, b2, b3; 
        uint32_t pc; 
    };
    static constexpr int STORE_LOG_N = 256;
    StoreLog m_storeLog[STORE_LOG_N]; 
    int m_storeLogPos = 0;
    bool m_duringReset = false;
    
    // Stack canary protection
    uint32_t m_canaryStart = 0;
    uint32_t m_canaryEnd = 0;
    static constexpr uint32_t CANARY_PATTERN = 0xAA55AA55;
    static constexpr uint32_t CANARY_SIZE = 8; // 8 bytes at each end
    
    // Physical memory access for stack corruption detection
    uint32_t readPhysBE32(uint32_t phys_addr) const;
    void writePhysBE32(uint32_t phys_addr, uint32_t value);
    uint32_t readPhysBE24(uint32_t phys_addr) const;
    void writePhysBE24(uint32_t phys_addr, uint32_t value);
    uint8_t readPhys8(uint32_t phys_addr) const;
    void writePhys8(uint32_t phys_addr, uint8_t value);
    
    // Stack corruption detection helpers
    void onCall(uint32_t ret_pc);
    bool onReturn(uint32_t popped_pc);
    void logStore(uint32_t addr, uint8_t size, uint32_t value);
    void dumpStackAround(uint32_t sp, uint32_t range);
    void dumpRecentStores(int count);
    void traceSPModification(uint32_t old_sp, uint32_t new_sp, const char* source);
    bool isValidVector(uint32_t addr, uint32_t val) const;
    void emulateTrap(uint8_t imm);
    void initStackCanary();
    bool checkStackCanary();
    
    // fw29.txt GPT5 instruction execution engine
    H8S2350InstructionDecoder m_decoder;
    // PERF-133 (2026-09-26): decoded-instruction cache. decode() is a pure function of the bytes at
    // pc (it reads nothing but memory), and it walked a long if-chain on every instruction (9 % of the
    // thread). Entries are kept only for code read through a side-effect-free path: flash in
    // READ_ARRAY mode and the DRAM. Invalidation: every flash bus write (commands, program, erase,
    // mode changes) and reset() bump m_dcacheGen; a DRAM write into a page holding cached code bumps
    // that page's generation and the previous page's (an instruction is up to 10 bytes long).
    // MS2K_DCACHE=off decodes every instruction afresh (A/B).
    struct DecodeCacheEntry { uint32_t pc = 0xFFFFFFFFu;
                                          uint32_t gen = 0; uint32_t pageGen = 0; uint8_t op0 = 0; uint8_t fk = 0; uint8_t raw[10] = {}; H8S2350InstructionExecutor::DirectFn fn = nullptr; H8S2350Instruction insn; };
    // PERF-MCU-1 (2026-10-01): the executors re-read their own instruction bytes through readByte()
    // (opcode, register fields, immediates, absolute addresses) - the whole bus path per byte. The
    // cache entry now keeps the bytes it decoded; while the instruction executes, readByte() answers
    // [pc0, pc0+size) from them. Off for the uncached decode path, the diagnostics that watch reads,
    // and from the moment a flash bus write or a DRAM write into a code page happens (conservative).
    const uint8_t* m_decodeRaw = nullptr;             // set by decodeCached(): the entry's bytes, or null
    uint8_t m_decodeFk = 0;                           // PERF-MCU-3: fused kind of the entry (0 = executor)
    H8S2350InstructionExecutor::DirectFn m_decodeFn = nullptr;   // PERF-MCU-6: the handler for FK_CALL
    static uint8_t fusedKind(const H8S2350Instruction& insn, const uint8_t* raw);
    const uint8_t* m_insnRaw = nullptr;
    bool m_insnRawOn = false;
    static constexpr uint32_t DCACHE_SIZE = 1u << 16;
    static constexpr uint32_t DRAM_PAGES  = 0x80000u >> 8;
    std::vector<DecodeCacheEntry> m_dcache;
    std::vector<uint32_t> m_dramPageGen;
    std::vector<uint8_t>  m_dramCodePage;
    uint32_t m_dcacheGen = 1;
    // Returns a reference that stays valid until the next decodeCached() call (the executor gets
    // it as the const& it always took; nothing decodes again while an instruction executes).
    const H8S2350Instruction& decodeCached(uint32_t pc);
    H8S2350Instruction m_decodeScratch;               // the uncached path's result
    struct CycAudit { uint64_t n = 0, direct = 0, ticked = 0; };
    CycAudit m_cycAudit[512];                         // CYC-AUDIT (MS2K_CYCAUDIT=1)
    uint8_t m_decodeOp0 = 0;                          // PERF-135: the byte at pc, read once with the decode
    inline void dramCodeWrite(uint32_t addr) {        // PERF-133: addr in 0x400000-0x47FFFF
        const uint32_t pg = (addr - 0x400000u) >> 8;
        if (pg < DRAM_PAGES && !m_dramCodePage.empty() && m_dramCodePage[pg]) { ++m_dramPageGen[pg]; if (pg) ++m_dramPageGen[pg - 1]; m_insnRawOn = false; }
    }
public:
    void invalidateDecodeCache() { ++m_dcacheGen; m_insnRawOn = false; }   // PERF-133: host-side changes to code memory
private:
    H8S2350InstructionExecutor m_executor;
    bool m_trace = false;
    bool isVerboseTrace() const { return (m_trace || m_trace_cpu_always) && !m_quietBoot; }
    
    // Verification counters for real vs stub execution
    uint64_t m_real_instruction_count = 0;
    uint64_t m_stub_instruction_count = 0;
    uint64_t m_total_instruction_count = 0;
    
    // Peripheral tick helper
    void tickPeripherals(uint32_t cycles);
    // PERF-134: event-driven peripheral ticking (see tickPeripherals()).
    void     tickPeripheralsNow(uint32_t cycles);   // the per-chunk body, unchanged
    void     peripheralsSync();                     // run the pending cycles now (before any I/O access)
    uint64_t peripheralBudget() const;              // cycles until the chunk that holds the next event
    bool     peripheralEventNow() const;            // level conditions: the next chunk must be ticked
    uint32_t m_perPend   = 0;                       // cycles not yet given to the peripherals
    uint64_t m_perBudget = 0;                       // 0 = tick the next chunk on its own
    bool     m_inTick    = false;                   // inside tickPeripheralsNow(): no re-entrant sync
    bool     m_perDmacFast  = false;                // PERF-136: no DTE set - the batching path keeps dmacStep's pace
    bool     m_dmacPaceDone = false;                // PERF-136: dmacStep() skips a pending flush it has nothing to do in
    
    // CPU state
    H8S2350Registers m_registers;
    H8SFlags m_flags;
    H8S2350Mode m_mode;
    TrapVectorConfig m_trap_cfg{};
    
    // Accurate PC attribution for memory writes (stack corruption debugging)
    uint32_t m_effectivePC;
    
    // Memory
    FlashROM m_flash_rom;                    // 1 MB Flash ROM (MBM29LV800B) @ CS0: 0x00000000-0x000FFFFF
    std::vector<uint8_t> m_ram;
    std::vector<uint8_t> m_cpu_ram;       // 512 KB CPU SRAM (V53C16256LK) at 0x00100000
    std::vector<uint8_t> m_external_memory;  // 2 MB external bus (MA0-20)
    // I/O registers (for compatibility with MAME) - REMOVED DUPLICATE
    
    // System Control Register (added from MAME)
    uint8_t m_syscr;
    // AUDIT-2: system registers the firmware writes at boot that used to be DISCARDED by the
    // legacy switch's default. Appendix B, RENDERED p.847 (printed 811). Latches only.
    uint8_t m_irqCtl[4]  = { 0, 0, 0, 0 };   // ISCRH, ISCRL, IER, ISR (H'FF2C-FF2F), all reset H'00 (p.132-134)
    uint8_t m_dtcer[8]   = { 0 };            // DTCERA-F H'FF30-FF35, [7] = DTVECR H'FF37
    uint8_t m_sbycr      = 0x08;             // H'FF38
    uint8_t m_sckcr      = 0x00;             // H'FF3A
    uint8_t m_mstpcr[2]  = { 0x3F, 0xFF };   // MSTPCRH/L H'FF3C-FF3D
    uint8_t m_pcrBE[4]   = { 0, 0, 0, 0 };   // PBPCR-PEPCR H'FF71-FF74 (MOS pull-ups)
    void    audit2CheckModuleStop();
    
    // GPIO state tracking (for automatic DDR configuration)
    std::vector<uint8_t> m_ddr_registers;  // Data Direction Registers
    std::vector<uint8_t> m_port_registers; // Port Data Registers
    
    // Peripherals
    std::shared_ptr<H8S2350PeripheralEmulator> m_peripheral;
    std::shared_ptr<H8S2350InterruptSystem> m_intc;
    
    
    // Timer and Interrupt System
    struct TimerSystem {
        uint16_t TCR;    // Timer Control Register
        uint16_t TSR;    // Timer Status Register
        uint16_t TCNT;   // Timer Counter
        uint16_t TGR[6]; // Timer General Registers (6 channels)
        uint32_t cycles; // Internal cycle counter
    };
    TimerSystem m_timer;
    
    // TPU (Timer Pulse Unit) - 6-channel 16-bit timer
    struct TPUSystem {
        uint16_t TCR;     // TPU Control Register
        uint16_t TMDR;    // TPU Mode Register
        uint16_t TIOR;    // TPU I/O Control Register
        uint16_t TIER;    // TPU Interrupt Enable Register
        uint16_t TSR;     // TPU Status Register
        uint16_t TCNT;    // TPU Counter
        uint16_t TGR[6];  // TPU General Registers (6 channels)
        uint16_t TIORH;   // TPU I/O Control Register High
        uint16_t TIORL;   // TPU I/O Control Register Low
        uint32_t cycles;  // Internal cycle counter
    };
    TPUSystem m_tpu;
    
    // PPG (Programmable Pulse Generator)
    struct PPGSystem {
        uint16_t PCSR;    // PPG Control/Status Register
        uint16_t PPR;     // PPG Period Register
        uint16_t PDR;     // PPG Data Register
        uint16_t PSR;     // PPG Status Register
        uint32_t cycles;  // Internal cycle counter
    };
    PPGSystem m_ppg;
    
    // Watchdog Timer
    struct WatchdogSystem {
        uint16_t WCR;     // Watchdog Control Register
        uint16_t WSR;     // Watchdog Status Register
        uint16_t WCNT;    // Watchdog Counter
        uint32_t cycles;  // Internal cycle counter
        bool timeout;     // Watchdog timeout flag
    };
    WatchdogSystem m_watchdog;
    
    // SCI (Serial Communication Interface) - 3 channels (fw8.txt Multi-SCI)
    struct SCISystem {
        uint16_t SMR;     // Serial Mode Register
        uint16_t BRR;     // Bit Rate Register
        uint16_t SCR;     // Serial Control Register
        uint16_t TDR;     // Transmit Data Register
        uint16_t RDR;     // Receive Data Register
        uint16_t SSR;     // Serial Status Register
        uint16_t SCMR;    // Serial Control Mode Register
        uint32_t cycles;  // Internal cycle counter
        bool txEnabled() const { return (SCR & 0x20) != 0; }  // TE bit
        bool isSynchronousMode() const { return (SMR & 0x80) != 0; }  // CM bit
        uint32_t effectiveBaud() const { return 31250 / ((BRR + 1) * 2); }  // Simplified calculation
        int dataBits() const { return (SMR & 0x40) ? 7 : 8; }  // CHR bit
        char parityChar() const { return (SMR & 0x20) ? ((SMR & 0x10) ? 'O' : 'E') : 'N'; }  // PE/PO bits
        int stopBits() const { return (SMR & 0x08) ? 2 : 1; }  // STOP bit
    };
    SCISystem m_sci[3];  // 3 SCI channels (fw8.txt Multi-SCI support)
    
    // A/D Converter - 10-bit resolution, 8 channels
    struct ADConverter {
        uint16_t ADCSR;   // A/D Control/Status Register
        uint16_t ADCR;    // A/D Control Register
        uint16_t ADDR[8]; // A/D Data Registers (8 channels)
        uint16_t ADDRH;   // A/D Data Register High
        uint16_t ADDRL;   // A/D Data Register Low
        uint32_t cycles;  // Internal cycle counter
        bool conversion_complete; // Conversion complete flag
        uint8_t current_channel;  // Current conversion channel
    };
    ADConverter m_adc;
    
    // D/A Converter - 8-bit resolution, 2 channels
    struct DAConverter {
        uint16_t DACR;    // D/A Control Register
        uint16_t DADR[2]; // D/A Data Registers (2 channels)
        uint16_t DADR0;   // D/A Data Register 0
        uint16_t DADR1;   // D/A Data Register 1
        uint32_t cycles;  // Internal cycle counter
        bool output_ready; // Output ready flag
    };
    DAConverter m_dac;
    
    // DMA Controller (DMAC) - 4 channels in short address mode, 2 in full address mode
    struct DMAController {
        uint16_t DCR;     // DMA Control Register
        uint16_t DSR;     // DMA Status Register
        uint16_t DOR;     // DMA Offset Register
        uint16_t DAR;     // DMA Address Register
        uint16_t DCR0;    // DMA Control Register 0
        uint16_t DCR1;    // DMA Control Register 1
        uint16_t DCR2;    // DMA Control Register 2
        uint16_t DCR3;    // DMA Control Register 3
        uint32_t SAR[4];  // Source Address Registers (4 channels)
        uint32_t DAR_array[4];  // Destination Address Registers (4 channels)
        uint16_t TCR[4];  // Transfer Count Registers (4 channels)
        uint32_t cycles;  // Internal cycle counter
        bool transfer_active[4]; // Transfer active flags
        uint8_t current_channel; // Current active channel
    };
    DMAController m_dmac;
    
    // Data Transfer Controller (DTC) - can be activated by internal interrupt or software
    struct DTCController {
        uint16_t DTCER;   // DTC Enable Register
        uint16_t DTCSR;   // DTC Status Register
        uint16_t DTCOR;   // DTC Offset Register
        uint16_t DTCAR;   // DTC Address Register
        uint16_t DTCER0;  // DTC Enable Register 0
        uint16_t DTCER1;  // DTC Enable Register 1
        uint16_t DTCER2;  // DTC Enable Register 2
        uint16_t DTCER3;  // DTC Enable Register 3
        uint32_t SAR[4];  // Source Address Registers (4 channels)
        uint32_t DAR[4];  // Destination Address Registers (4 channels)
        uint16_t TCR[4];  // Transfer Count Registers (4 channels)
        uint32_t cycles;  // Internal cycle counter
        bool transfer_active[4]; // Transfer active flags
        uint8_t current_channel; // Current active channel
    };
    DTCController m_dtc;
    
    // I/O Register System - handles 0x00FF0000-0x00FFFFFF range
    // H8S/2350 I/O Register System - Based on hardware specification YAML
    struct IORegisterSystem {
        // CPU Control Registers (per YAML spec)
        uint8_t SYSCR;    // System Control Register (0xFF39, reset=0x01)
        uint8_t MDCR;     // Mode Control Register (0xFF3B, read-only, MD2..0 pins)
        
        // Interrupt Control Unit (ICU) Registers (per YAML spec)
        uint8_t ISCRH;    // IRQ Sense Control High (0xFF2C, reset=0x00)
        uint8_t ISCRL;    // IRQ Sense Control Low (0xFF2D, reset=0x00)
        uint8_t IER;      // IRQ Enable Register (0xFF2E, reset=0x00)
        uint8_t ISR;      // IRQ Status Register (0xFF2F, reset=0x00)
        uint8_t IPR;      // General Interrupt Priority Register (0x0202, reset=0x00)
        
        // Interrupt Priority Registers A-K (per YAML spec)
        uint8_t IPRA;     // Interrupt Priority Register A (0xFEC4, reset=0x77)
        uint8_t IPRB;     // Interrupt Priority Register B (0xFEC5, reset=0x77)
        uint8_t IPRC;     // Interrupt Priority Register C (0xFEC6, reset=0x77)
        uint8_t IPRD;     // Interrupt Priority Register D (0xFEC7, reset=0x77)
        uint8_t IPRE;     // Interrupt Priority Register E (0xFEC8, reset=0x77)
        uint8_t IPRF;     // Interrupt Priority Register F (0xFEC9, reset=0x77)
        uint8_t IPRG;     // Interrupt Priority Register G (0xFECA, reset=0x77)
        uint8_t IPRH;     // Interrupt Priority Register H (0xFECB, reset=0x77)
        uint8_t IPRI;     // Interrupt Priority Register I (0xFECC, reset=0x77)
        uint8_t IPRJ;     // Interrupt Priority Register J (0xFECD, reset=0x77, DMAC+SCI0)
        uint8_t IPRK;     // Interrupt Priority Register K (0xFECE, reset=0x77, SCI1)
        
        // Bus Controller Registers (per YAML spec)
        uint8_t ABWCR;    // Area Bus Width Control Register (0xFED0)
        uint8_t ASTCR;    // Access State Control Register (0xFED1, reset=0xFF)
        uint8_t WCRH;     // Wait Control Register High (0xFED2, reset=0xFF)
        uint8_t WCRL;     // Wait Control Register Low (0xFED3, reset=0xFF)
        uint8_t BCRH;     // Bus Control Register High (0xFED4)
        uint8_t BCRL;     // Bus Control Register Low (0xFED5)
        uint8_t MCR;      // Memory Control Register (0xFED6)
        uint8_t DRAMCR;   // DRAM Control Register (0xFED7)
        uint8_t RTCNT;    // Refresh Timer Counter (0xFED8)
        uint8_t RTCOR;    // Refresh Timer Constant Register (0xFED9)
        
        // SCI0 Registers (per YAML spec) - MIDI interface
        uint8_t SCI0_SMR; // SCI0 Serial Mode Register (0xFF78, reset=0x00)
        uint8_t SCI0_BRR; // SCI0 Bit Rate Register (0xFF79, reset=0xFF)
        uint8_t SCI0_SCR; // SCI0 Serial Control Register (0xFF7A, reset=0x00)
        uint8_t SCI0_TDR; // SCI0 Transmit Data Register (0xFF7B, reset=0xFF)
        uint8_t SCI0_SSR; // SCI0 Serial Status Register (0xFF7C, reset=0x84)
        uint8_t SCI0_RDR; // SCI0 Receive Data Register (0xFF7D, reset=0x00)
        uint8_t SCI0_SCMR; // SCI0 Serial Control Mode Register (0xFF7E, reset=0xF2)
        
        // Port Registers (keeping existing for compatibility)
        uint8_t P1DDR;    // Port 1 Data Direction Register
        uint8_t P1DR;     // Port 1 Data Register
        uint8_t P2DDR;    // Port 2 Data Direction Register
        uint8_t P2DR;     // Port 2 Data Register
        uint8_t P3DDR;    // Port 3 Data Direction Register
        uint8_t P3DR;     // Port 3 Data Register
        uint8_t P4DDR;    // Port 4 Data Direction Register
        uint8_t P4DR;     // Port 4 Data Register
        uint8_t P5DDR;    // Port 5 Data Direction Register
        uint8_t P5DR;     // Port 5 Data Register
        uint8_t P6DDR;    // Port 6 Data Direction Register
        uint8_t P6DR;     // Port 6 Data Register
        uint8_t P7DDR;    // Port 7 Data Direction Register
        uint8_t P7DR;     // Port 7 Data Register
        uint8_t P8DDR;    // Port 8 Data Direction Register
        uint8_t P8DR;     // Port 8 Data Register
        uint8_t P9DDR;    // Port 9 Data Direction Register
        uint8_t P9DR;     // Port 9 Data Register
        uint8_t PADDR;    // Port A Data Direction Register
        uint8_t PADR;     // Port A Data Register
        uint8_t PBDDR;    // Port B Data Direction Register
        uint8_t PBDR;     // Port B Data Register
        uint8_t PCDDR;    // Port C Data Direction Register
        uint8_t PCDR;     // Port C Data Register
        uint8_t PDDDR;    // Port D Data Direction Register
        uint8_t PDDR;     // Port D Data Register
        uint8_t PEDDR;    // Port E Data Direction Register
        uint8_t PEDR;     // Port E Data Register
        uint8_t PFDDR;    // Port F Data Direction Register
        uint8_t PFDR;     // Port F Data Register
        uint8_t PGDDR;    // Port G Data Direction Register
        uint8_t PGDR;     // Port G Data Register
        uint8_t PHDDR;    // Port H Data Direction Register
        uint8_t PHDR;     // Port H Data Register
        
        // Legacy registers (kept for compatibility)
        uint8_t MSTCR;    // Master Control Register
        uint8_t WCR;      // Watchdog Control Register
        uint8_t WSR;      // Watchdog Status Register
        uint8_t TCNT;     // Timer Counter
        uint8_t TCR;      // Timer Control Register
        uint8_t TSR;      // Timer Status Register
        
        // MS2000-specific LCD and DSP interface registers
        uint8_t LCD_CTRL; // LCD Control Register
        uint8_t LCD_DATA; // LCD Data Register
        uint8_t LCD_STATUS; // LCD Status Register
        uint8_t DSP_CTRL; // DSP Control Register
        uint8_t DSP_DATA; // DSP Data Register
        uint8_t DSP_STATUS; // DSP Status Register
        
        // MS2000 custom registers for compatibility
        uint8_t LOW_CTRL, LOW_STATUS, LOW_DATA, LOW_CONFIG;
        uint8_t MS2000_CTRL, MS2000_STATUS, MS2000_DATA, MS2000_CONFIG;
        uint8_t MM_PERIPH_CTRL, MM_PERIPH_STATUS, MM_PERIPH_DATA, MM_PERIPH_CONFIG;
        uint8_t ADV_SYS_CTRL, ADV_SYS_STATUS, ADV_SYS_DATA, ADV_SYS_CONFIG;
        
        // i11.txt: H8S system control registers
        uint8_t MSTPCR = 0x00;    // Module Stop Control Register (0xFFFC) - 0=enabled
        uint8_t SBYCR = 0x00;     // Standby Control Register (0xFFFE)

        // ------------------------------------------------------------------
        // DMAC register block, 0xFFFF00-0xFFFF07 (2026-09-13).
        // Renesas H8S/2350 HM Rev 3.00, Appendix B.1:
        //   FF00 DMAWER   FF01 DMATCR   FF02 DMACR0A  FF03 DMACR0B
        //   FF04 DMACR1A  FF05 DMACR1B  FF06 DMABCRH  FF07 DMABCRL
        //
        // THIS IS STORAGE, NOT A DMA CONTROLLER. It does not transfer anything.
        // It exists because the block was entirely unmapped and every read
        // returned the 0xFF unmapped default, which told the firmware that
        // DMABCRL bit 5 (DTE0B, "data transfer enabled") was SET - so its
        // wait-for-transfer-complete poll at 0x010C58 span 1,258,188 times on a
        // transfer it had never started. Per the rendered page the reset value
        // of every DTE bit is 0, so on real hardware that loop falls straight
        // through on the first pass.
        //
        // "Storage is the honest minimum: it lies about behaviour but not about
        // state." A real DMAC - MAR/ETCR/IOAR, the transfer itself, and clearing
        // DTE at terminal count - is still owed.
        // ------------------------------------------------------------------
        uint8_t DMAC_FF00_FF07[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        // 2026-09-13: the DMAC's per-channel address and count registers.
        // Renesas H8S/2350 HM Rev 3.00, Table 7.3, RENDERED PDF page 236
        // (printed "Section 7, 7.1.5 Register Configuration"):
        //   ch0: MAR0A H'FEE0  IOAR0A H'FEE4  ETCR0A H'FEE6
        //        MAR0B H'FEE8  IOAR0B H'FEEC  ETCR0B H'FEEE
        //   ch1: MAR1A H'FEF0  IOAR1A H'FEF4  ETCR1A H'FEF6
        //        MAR1B H'FEF8  IOAR1B H'FEFC  ETCR1B H'FEFE
        // This whole block was UNMAPPED - reads answered 0xFF, writes vanished -
        // which is destructive, not neutral, and it hid what the firmware actually
        // programs. Storage first, then the model.
        uint8_t DMAC_FEE0_FEFF[0x20] = {0};
        
        // i11.txt: SCI registers - use different names to avoid conflicts
        // SCI0 (0xFF80-0xFF86)
        uint8_t SCI0_SMR_NEW = 0x00;  // Serial Mode Register
        uint8_t SCI0_BRR_NEW = 0xFF;  // Bit Rate Register
        uint8_t SCI0_SCR_NEW = 0x00;  // Serial Control Register
        uint8_t SCI0_TDR_NEW = 0xFF;  // Transmit Data Register
        uint8_t SCI0_SSR_NEW = 0x80;  // Serial Status Register (TDRE=1)
        uint8_t SCI0_RDR_NEW = 0x00;  // Receive Data Register
        
        // SCI1 (0xFF88-0xFF8E)
        uint8_t SCI1_SMR = 0x00;
        uint8_t SCI1_BRR = 0xFF;
        uint8_t SCI1_SCR = 0x00;
        uint8_t SCI1_TDR = 0xFF;
        uint8_t SCI1_SSR = 0x80;  // TDRE=1
        uint8_t SCI1_RDR = 0x00;
        
        // SCI2 (0xFF90-0xFF96)
        uint8_t SCI2_SMR = 0x00;
        uint8_t SCI2_BRR = 0xFF;
        uint8_t SCI2_SCR = 0x00;
        uint8_t SCI2_TDR = 0xFF;
        uint8_t SCI2_SSR = 0x80;  // TDRE=1
        uint8_t SCI2_RDR = 0x00;
    };
    IORegisterSystem m_io_registers;
    
    // Advanced Mode Bus Controller
    AdvancedModeBusController m_advanced_mode;
    
    // Interrupt System
    struct InterruptSystem {
        uint8_t IER;     // Interrupt Enable Register
        uint8_t ISR;     // Interrupt Status Register
        uint8_t IPR;     // Interrupt Priority Register
        bool nmi_pending; // NMI pending flag
        bool irq_pending; // IRQ pending flag
    };
    InterruptSystem m_interrupt;
    
    
    // Execution state
    bool m_debug_mode;
    bool m_trace_cpu_always;  // lcd5.txt: Prevent CPU trace muting after I/O scan
    bool m_quietBoot;         // FIX21: Suppress per-instruction verbose trace for fast boot
    uint32_t m_cycles_executed;

    // Replay Debugger
    std::unique_ptr<ReplayLogger> m_replay_logger;
    bool m_replay_mode = false;
    std::string m_replay_filename;
    bool m_record_mode = false;
    std::string m_record_filename;

    // LCD Display connection (for firmware communication)
    RealLCDDisplay* m_lcd_display;
    uint8_t m_lcd_cursor_position;
    
    // LCD Callback system for firmware communication
    LCDWriteCallback m_lcd_write_callback;
    LCDReadCallback m_lcd_read_callback;
    
    // MS2000 GPIO-level LCD adapter (h8s.txt implementation)
    std::unique_ptr<Ms2kLcdAdapter> m_gpio_lcd_adapter;

    // === P2DR 4-bit LCD bit-bang (UKNTCH2000 reference, src/lcd.c) ===
    // Real HW: LCD hangs off Port 2 (P2DR=0xFF61). Bits: 4=E, 5=RW, 6=RS, 0-3=data nibble.
    // E rising edge latches a nibble; two nibbles form one byte -> dispatched to LCD adapter.
    // === BUG54: the PANEL / KEYBOARD SCAN MATRIX (KOD-A30411) ===
    // P50-P52 drive A/B/C of two 74LV138A 3-to-8 decoders (IC16 -> BR0..BR5,
    // IC11 -> MK0..MK5; Y6 and Y7 are NU on both), P53 selects the bank, and the
    // eight return lines T0-T7 are read on PORT6 (H'FFFF55). Every T line carries a
    // 10k pull-up to the 3.3 V rail, so an unselected or unpressed line reads 1.
    //
    // Indexed by the four-bit P5DR select. A bit reads 0 when a contact on the
    // selected column is closed; the key and button sources are not wired yet, so
    // every bit that a HUMAN could close is 1. When they are wired, THIS is the
    // single place that changes and PORT6 needs no further edits.
    //
    // === BUG63: D51 - THE IDLE PATTERN IS *NOT* ALL-0xFF, AND THIS IS NOT A HACK ===
    // Service manual KOD-A30414, "KLM-2181/82/83 Schematic (SW Matrix)", rendered:
    // IC27 (HD74HC138P) on the PANEL board takes A/B/C from CN4-9/10/11 and its
    // enable G1 from CN4-12, and drives SEVEN columns SS0..SS6 (Y7 is unlabelled and
    // unused - which is why the firmware's classifier ANDs bytes 0..6 and not 0..7).
    // Fifty switches SW1..SW50 sit on the grid, each in series with its own diode
    // D1..D50, returning on T0..T7 -> CN4-5/4/3/2/8/7/1/6 -> PORT6 bits 0..7.
    //
    // At the intersection of column SS0 and row T0 the sheet shows ONE MORE DIODE,
    // D51, inside a dashed box marked "for version up" - WITH NO SWITCH. It is a
    // hard strap from T0 to SS0.
    //
    // IT IS FITTED, and the manual's own parts list proves it rather than the box:
    //     TSW EVQ11A09K ........ KLM-2181/82/83 ... 50      fifty switches
    //     DIODE 1SS-133 T-77 ... KLM-2181/82/83 ... 51      FIFTY-ONE diodes
    // Fifty switches, fifty-one diodes. The fifty-first is the one with no switch.
    //
    // So on a healthy machine with nothing held, scan column 0 reads T0 LOW:
    // byte0 = 0xFE. The classifier at 0x000CFE ANDs bytes 0..6, gets 0xFE != 0xFF,
    // and RTSes -> 0x0008C0 -> JMP @0x002000, the normal firmware. All-0xFF is what
    // a machine with a DEAD PANEL SCAN reads, and it is why such a machine comes up
    // in the IPL showing "IPL s.p.u" - the symptom the field reports describe.
    // D51 is the panel's proof-of-life strap, and R3 is satisfied by modelling the
    // component, not by forcing the branch.
    uint8_t m_panelMatrix[16] = { 0xFE,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
                                  0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF };

    // BUG65: the port data direction block H'FEB0-H'FEBF, as storage. Ten of these
    // were silently discarded until now; P2DDR (H'FEB1) = 0xFF is the one that
    // decides what a port-2 read returns (rendered page 378).
    uint8_t m_portDDR[16] = {0};
    // DSP-RESET (2026-09-27): port 3 (HM 9.4, RENDERED p.387-388: P3DDR H'FEB2 W, P3DR H'FF62 R/W, P3ODR
    // H'FF76, all H'00 at power-on). P35 = PORT_RESET (KOD-A30411) -> DSP RESET (KOD-A30412), R137 pull-down.
    uint8_t m_p3odr = 0;
    bool    m_portResetHigh = false;
    void    updatePortReset();

    // BUG75: IPRA..IPRK, H'FEC4-H'FECE. "The IPR registers are initialized to H'77
    // by a reset" - Renesas HM Rev 3.00 section 5.2.2, RENDERED PDF page 131
    // (printed "95"). H'77 means EVERY source starts at level 7; the firmware then
    // programs the real ladder at 0x00209A-0x0020E0, and those eleven writes were
    // being discarded until this block existed.
    uint8_t m_ipr[11] = { 0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77 };

    // BUG76 instrument: windows in which a DTE bit was armed but dmacStep()'s 64-cycle
    // pace had not elapsed. Counts the exact thing BUG75's "pacing race" reading claimed.
    uint64_t m_dte_armed_while_paced = 0;
    bool     m_dte_refusal_reported  = false;   // one refusal report per DTE arm

    // BUG77: SCI0 finally has a transmitter instead of a hardcoded status byte.
    // Mirrors the SCI1 pair that BUG43 built (m_sci1_tx_active / m_sci1_tx_busy_cycles).
    bool     m_sci0_tx_active      = false;
    uint64_t m_sci0_tx_busy_cycles = 0;
    bool     m_sci0_timing_told    = false;   // print the character time once

    // === BUG78: THE A/D CONVERTER, H'FF90-H'FF99 ===
    // Register map and semantics from RENDERED PDF pages 681 (printed "645", ADDRA-D),
    // 682 ("646", ADCSR), 683 ("647", ADST/SCAN/CKS), 684 ("648", CH2-CH0 + ADCR) and
    // 689 ("653", section 15.4.2 Scan Mode). Reset values are the pages' own.
    uint16_t m_adc_addr[4]      = {0, 0, 0, 0};   // ADDRA..ADDRD, 10 bits LEFT-ALIGNED
    uint8_t  m_adc_adcsr        = 0x00;           // page 682: "initialized to H'00 by a reset"
    uint8_t  m_adc_adcr         = 0x3F;           // page 684: "initialized to H'3F by a reset"
    bool     m_adc_converting   = false;
    uint64_t m_adc_busy_cycles  = 0;
    uint8_t  m_adc_channel      = 0;              // the channel currently in the converter
    bool     m_adc_told         = false;          // one-shot configuration report
    // THE ANALOG SIDE IS NOT MODELLED AND THIS ARRAY IS WHERE THAT IS ADMITTED. Eight
    // 10-bit inputs, all zero = every pot at its minimum, which is a state the hardware
    // can actually be in. It is NOT a reading of a real panel - see the block comment.
    // AUDIT-1 (2026-10-09): AN0..AN3 are the rear analog inputs, not "0 on this model". KOD-A30411 p.14: AN0/AN1
    // pulled to ground by 4.7k on the MS2000R (keyboard model: bender / mod wheel). KOD-A30413 p.16: AN3 = SW_PEDAL
    // (FOOT SW jack PH1, R8 10k pull-up to +5 V: open = full scale), AN2 = ASS_PEDAL (PEDAL jack PH2, ring normalled to
    // the 5 V tip feed through R27: no plug = full scale, INFERRED from the jack symbol). Found by the firmware's own
    // factory test (FtCtrl / FootSW items waited forever at 0). Nothing plugged in = the defaults below.
    uint16_t m_adc_input[8]     = {0, 0, 0x3FF, 0x3FF, 0, 0, 0, 0};
public:
    void setRearAnalog(unsigned an, uint16_t value10) { if (an < 4) m_adc_input[an] = uint16_t(value10 > 1023 ? 1023 : value10); }   // AN0..AN3 (pedal / foot switch)
private:
    void     adcStep(uint32_t cycles);
    void     adcStartConversion();
    uint64_t adcChannelCycles() const;
    uint8_t  adcLastChannel() const;

    // === BUG79: PORT A, and the one bit of it the firmware ever reads ===
    // PORTA H'FF59 (read-only pins), PADR H'FF69, PAPCR H'FF70, PAODR H'FF77 - all four
    // were unmapped. PADDR H'FEB9 already has storage from BUG65 (m_portDDR[9]).
    uint8_t m_porta_dr   = 0x00;   // PADR   output latch
    uint8_t m_porta_pcr  = 0x00;   // PAPCR  input pull-up MOS control
    uint8_t m_porta_odr  = 0x00;   // PAODR  open-drain control
    bool    m_porta_told = false;  // one-shot strap report
    uint8_t portAPins() const;

    uint8_t m_p2dr_output = 0;   // last value written to P2DR; a read returns this latch
    uint8_t m_p2dr_shift  = 0;   // nibble assembly shift register
    uint8_t m_p2dr_phase  = 0;   // bit0: nibble phase (0=expect high), bit7: last E level
    void    writeP2DR(uint8_t value);
    uint8_t readP2DR() const;

    // === FIX24: TPG/TPU channels 2+4, faithful port of UKNTCH2000 src/h8s_tpg.c ===
    // Registers (short addr): TSTR=0xFFC0 TSYR=0xFFC1; ch2: TCR2=0xFFF0 TMDR2=0xFFF1
    // TIOR2=0xFFF2 TIER2=0xFFF4 TSR2=0xFFF5 TCNT2=0xFFF6/7 TGR2A=0xFFF8/9 TGR2B=0xFFFA/B;
    // ch4: TCR4=0xFE90 TMDR4=0xFE91 TIOR4=0xFE92 TIER4=0xFE94 TSR4=0xFE95 TCNT4=0xFE96/7
    // TGR4A=0xFE98/9 TGR4B=0xFE9A/B. Vectors: TGI2A/B/V/U=44-47, TGI4A/B/V/U=56-59.
    struct TPGState {
        uint32_t cntr = 0;
        uint8_t  irqlatch = 0;
        uint8_t  tstr = 0, tsyr = 0;
        uint8_t  tcr2 = 0, tmdr2 = 0xC0, tior2 = 0, tier2 = 0x40, tsr2 = 0xC0;
        uint16_t tcnt2 = 0, tgr2a = 0xFFFF, tgr2b = 0xFFFF;
        uint8_t  tcr4 = 0, tmdr4 = 0xC0, tior4 = 0, tier4 = 0x40, tsr4 = 0xC0;
        uint16_t tcnt4 = 0, tgr4a = 0xFFFF, tgr4b = 0xFFFF;
        // BUG115: channel 1, off the manual (Table 10.3, RENDERED p.454): TCR1 H'FFE0,
        // TMDR1 H'FFE1, TIOR1 H'FFE2, TIER1 H'FFE4, TSR1 H'FFE5, TCNT1 H'FFE6, TGR1A H'FFE8,
        // TGR1B H'FFEA; initial values as listed there.
        uint8_t  tcr1 = 0, tmdr1 = 0xC0, tior1 = 0, tier1 = 0x40, tsr1 = 0xC0;
        uint16_t tcnt1 = 0, tgr1a = 0xFFFF, tgr1b = 0xFFFF;
    } m_tpg;
    // === DMAC, short address mode (2026-09-13) ===
    // Renesas H8S/2350 HM Rev 3.00, Section 7, RENDERED pages 236 (Table 7.3 register
    // map), 242 (RPE/DTDIR), 243-244 (DTF activation sources), 247 (DTA), 249 (DTE/DTIE)
    // and 140 (vector table: DEND0A=72, DEND0B=73, DEND1A=74, DEND1B=75).
    // Storage for these registers already exists in m_io_registers; this is the engine.
    // === SCI1 transmit timing (2026-09-13, BUG43) ===
    // SCI1 is MIDI OUT (KOD-A30411: TXD1 pin 60 -> MIDI_OUT), and the firmware's
    // TXI1 handler drains a FIFO in DRAM one byte per transmit-empty interrupt.
    // Raising TXI the instant TDR is written makes that handler run back to back
    // with no bound; the transmitter has to take a character time.
    void     sci1TxStep(uint32_t cycles);
    uint64_t sci1CharCycles() const;
    // BUG77: the same derivation for SCI0, and a transmitter that takes time.
    uint64_t sci0CharCycles() const;
    void     sci0TxStep(uint32_t cycles);
    // BUG77c: step [1]/[2] of RENDERED page 635 (printed 599). The transmitter is driven by
    // the STATE of TDRE, not by the event of a TDR write - see the definition for why that
    // distinction stranded the whole link.
    void     sci0TryLoadTsr();
    void     sci0TdreSet();     // TDRE goes 0 -> 1: raise TXI0, and TXI0 activates the DMAC
    // BUG105: end of an SCI0 character = one SPI byte to/from the DSP's SHI (SDIR honoured,
    // SS = PF1 low). Clocked synchronous: TX and RX are the same eight clocks.
    void     sci0SpiCharacterDone();
    uint32_t fetchExtraStates(uint32_t pc, uint32_t size, uint8_t op0) const;   // Table A.4 S_I
    // BUG108: SCI1 receiver = MIDI IN.
    void     sci1RxStep(uint32_t cycles);
    std::mutex          m_midiInMutex;
    std::deque<uint8_t> m_midiInHost;      // pushed by the host MIDI port (any thread)
    std::atomic<bool>   m_midiInHostPending{false};   // PERF-132: set under the mutex by midiInPush; the CPU thread locks only then
    std::deque<uint8_t> m_sci1RxLine;      // bytes on the line, in order
    bool     m_sci1RxActive = false;
    uint64_t m_sci1RxBusy = 0;
    uint8_t  m_sci1RxByte = 0;
    uint64_t m_sci1RdrfAt = 0;              // cycle RDRF was last set (overrun instrument)
    struct MidiSched { uint64_t atCycle = 0; std::vector<uint8_t> bytes; };
    std::vector<MidiSched> m_midiSched;    // MS2K_MIDIIN
    size_t   m_midiSchedIdx = 0;
public:
    // Host MIDI IN (e.g. a MIDI port callback). Thread-safe; bytes go onto the MIDI IN line and
    // reach the firmware through SCI1's receiver at MIDI speed.
    void     midiInPush(const uint8_t* data, size_t n);
    DSP56362Emulator* dsp() const { return m_dsp.get(); }   // null when not built

    // Panel switches (GUI-2). KOD-A30414 "KLM-2181/82/83 Schematic (SW Matrix)", service manual
    // p.18: IC27 HC138 drives column SS0..SS6 = PORT5 select 0..6 (G2A/G2B low), a closed
    // switch pulls its return line T0..T7 = PORT6 bit 0..7 low through its diode. Thread-safe;
    // held keys are ANDed onto the strap matrix (D51) on every PORT6 read.
    void setPanelSwitch(unsigned column, unsigned row, bool pressed) {
        if (column > 6 || row > 7) return;
        const uint64_t bit = 1ull << (column * 8 + row);
        if (pressed) m_panelHeld.fetch_or(bit); else m_panelHeld.fetch_and(~bit);
    }
    // PANEL-IO (2026-09-27), all read off the service manual, RENDERED:
    //  - KNOBS, KOD-A30415 (p.19): four HC4051 muxes IC1..IC4, inputs X0..X7 picked by ADSEL2..0, outputs
    //    to AN4..AN7 (CN5-37..40); each pot is a 10k linear divider across 5 V. The A/D scans AN4-AN7
    //    (the firmware's ADCSR = 0x3F). setPanelKnob(mux, x, 0..1023) = the pot's reading.
    //  - ADSEL2..0 = P17..P15 through IC3 HCT08 buffers, LD08..LD11 = P10..P13, CODEC_MUTE = P14 (KOD-A30411
    //    p.14, P1DR H'FF60 per HM 9.2 RENDERED p.365); LD00..LD07 = IC23 LV574A, clocked by a write to CS1
    //    (area 1, 0x200000) on MD15..MD08 = the even byte.
    //  - LEDS, KOD-A30416 (p.20): IC6 HC138 (ADSEL) selects row LSn through a PNP, LDm switches column m through
    //    an NPN: LED (n, m) is lit while ADSEL = n and LDm = 1. The firmware multiplexes a row every 2 ms, so
    //    what an eye sees is the duty: panelLeds() returns, per LED, the lit share of its row's time since
    //    the previous call.
    void setPanelKnob(unsigned mux, unsigned x, uint16_t value10) {
        if (mux < 4 && x < 8) m_knob[mux][x].store(uint16_t(value10 > 1023 ? 1023 : value10), std::memory_order_relaxed);
    }
    // MIDI OUT (2026-09-27): every byte the firmware writes to TDR1 (SCI1 = MIDI OUT, KOD-A30411 TXD1 pin 60),
    // handed on from the emulation thread in the order written. Thread-safe to set.
    void setMidiOutSink(std::function<void(uint8_t)> s) { std::lock_guard<std::mutex> l(m_midiOutMx); m_midiOutSink = std::move(s); }
    struct PanelLeds { float lit[8][12]; bool codecMute; };
    // KNOB-FOLLOW (2026-10-01) diagnostic: where in RAM a byte string sits (host-side, no bus cycle). Prints
    // every hit as (buffer, offset, CPU address) and the 254 bytes from there (one TABLE 1 program).
    // KNOB-FOLLOW: host-side read of the external memory (the DRAM image) - no bus cycle, no side effect
    uint8_t peekExternal(uint32_t addr) const { return addr < m_external_memory.size() ? m_external_memory[addr] : uint8_t(0); }
    void debugFindBytes(const std::string& needle) {
        auto scan = [&](const std::vector<uint8_t>& v, const char* nm, uint32_t base) {
            for (size_t i = 0; i + needle.size() <= v.size(); ++i) {
                if (std::memcmp(v.data() + i, needle.data(), needle.size()) != 0) continue;
                printf("[FIND] '%s' in %s +0x%05zX = CPU 0x%06zX:", needle.c_str(), nm, i, size_t(base) + i);
                for (size_t k = 0; k < 254 && i + k < v.size(); ++k) printf("%s%02X", k % 32 ? " " : "\n[FIND]   ", v[i + k]);
                printf("\n");
            }
        };
        scan(m_cpu_ram, "cpu_ram", H8S2350MemoryMap::CPU_RAM_START);
        scan(m_ram, "onchip_ram", 0xFFF400u);
        scan(m_external_memory, "external", 0u);
        fflush(stdout);
    }
    PanelLeds panelLeds();
private:
    std::atomic<uint16_t> m_knob[4][8] = {};
    uint8_t  m_p1dr = 0, m_ldLatch = 0;
    uint64_t m_ledLast = 0, m_ledOn[8][12] = {}, m_ledRowT[8] = {};
    float m_ledPrev[8][12] = {};   // a row not scanned since the last panelLeds() keeps its last duty
    std::mutex m_ledMx;
    std::mutex m_midiOutMx;
    std::function<void(uint8_t)> m_midiOutSink;
    void     ledAccount();                 // charge the time since the last panel write to the lit LEDs
    void     panelTestIoStep();            // MS2K_PANELKNOBS / MS2K_LEDDUMP (test input / R2 diagnostic)
    // BUS-DATA-STATES (see busInsnStates in the .cpp)
    void     busRecord(uint32_t address, bool word, bool write) {
        address &= 0xFFFFFFu;
        if (address >= 0xF80000u && (address < 0xF82000u || address >= 0xFFF400u)) return;   // on-chip (and its alias)
        if (address - m_busPc0 < m_busInsnSize) return;               // the executor re-reading its instruction
        if (m_busRecN < kBusRecMax) m_busRec[m_busRecN++] = { address, uint8_t((write ? 1u : 0u) | (word ? 2u : 0u)) };
        else ++m_busRecLost;
    }
    void     busConfigUpdate();
    struct BusArea { bool dram; uint8_t m, sByte, sWord; } m_busArea[8] = {};
    uint32_t m_busRowShift = 9, m_busRefreshAvail = 0, m_busRefreshLen = 0;
    uint32_t busCycle(uint32_t a, bool write, bool word, uint32_t& idle);
    uint32_t busInsnStates(uint32_t pc, uint32_t size, uint8_t op0, uint32_t baseCycles);
    static constexpr uint32_t kBusRecMax = 16;
    struct BusRec { uint32_t a; uint8_t fl; } m_busRec[kBusRecMax] = {};
    uint32_t m_busRecN = 0, m_busRecLastN = 0, m_busPc0 = 0, m_busInsnSize = 0, m_busLastArea = 0, m_busDramRow = 0, m_busRefreshAcc = 0;
    bool     m_busInExec = false, m_busLastExt = false, m_busLastRead = false, m_busLastDram = false, m_busDramOpen = false;
    int      m_busDepth = 0;
    uint64_t m_busRecLost = 0, m_busDataNextPrint = 0;
    uint64_t m_bsBase = 0, m_bsOld = 0, m_bsFetch = 0, m_bsData = 0, m_bsIdle = 0, m_bsRefresh = 0;
    std::atomic<uint64_t> m_panelHeld{0};
    uint8_t  m_sci0_tsr = 0;    // the byte in TSR (loaded from TDR by sci0TryLoadTsr)
    bool     m_dmacWritingTdr = false;   // BUG108: set while the DMAC performs its write
    std::unique_ptr<DSP56362Emulator> m_dsp;   // null = not built (ROM missing / MS2K_DSP=off)
    uint64_t m_sci1_tx_busy_cycles = 0;   // >0 while a character is on the wire
    bool     m_sci1_tx_active = false;

    void dmacStep(uint32_t cycles);
    bool dmacServiceSubchannel(int ch, bool isB);   // one transfer if armed and ready
    uint32_t m_dmac_pace_accum = 0;

    void tpgReset();
    void tpgStep(uint32_t cycles);
    bool tpgWrite(uint32_t a16, uint8_t value);   // true if handled
    bool tpgRead(uint32_t a16, uint8_t& out);     // true if handled

    // === FIX26: SCI0 = DSP serial link (UKNTCH2000 ref main.c) ===
    // On the real MS2000, SCI0 talks to the DSP56362 (SCI1 = MIDI). The reference
    // boots the FW to photo-proven LCD feeding SCI0 RX from an LCG PRNG (sic!).
    // SSR0=0xFF7C (RDRF=0x40 kept set, rx always available), RDR0=0xFF7D.
    int     m_sci0_rng = 0;
    uint8_t m_sci0_tdr = 0;   // last byte FW sent toward DSP (future protocol use)
    uint8_t m_sci0_scr = 0;   // SCR0 shadow: TIE=0x80 RIE=0x40 TE=0x20 RE=0x10
                              // Vectors: ERI0=80, RXI0=81, TXI0=82 (table offsets 0x140/144/148)
    
    // Panel Interface connection (for virtual keyboard and controls)
    PanelIF* m_panel_interface;
    
    // MP (Multiplexer Panel) Stub - CPU → MP → LCD architecture
    std::unique_ptr<MS2000MPStub> m_mp_stub;
    std::unique_ptr<MS2000PanelADC> m_panel_adc;
    std::unique_ptr<MS2000SwitchMatrix> m_switch_matrix;
    std::unique_ptr<MS2000LEDMatrix> m_led_matrix;
    std::unique_ptr<MS2000MIDIInterfaceSimple> m_midi_interface;
    
    // Clock system
    uint32_t m_clock_frequency;      // Clock frequency in Hz (default: 20MHz)
    uint32_t m_clock_cycles_per_step; // Cycles per emulation step
    bool m_high_speed_mode;          // High speed mode for faster execution
    
    // SIMD Optimization System
    SIMDOptimizationConfig m_simd_config;
    std::unique_ptr<SIMDInstructionCache> m_simd_cache;
    std::unique_ptr<SIMDHotMemoryPool> m_simd_hot_memory;
    alignas(64) uint64_t m_simd_total_executions = 0;
    alignas(64) uint64_t m_simd_vectorized_ops = 0;
    
    bool m_halted;

    // ---------------------------------------------------------------------
    // PC RING-LOG (2026-09-13). Records the last N instruction boundaries and
    // dumps them when the machine derails, so the instruction that wrote the
    // bad PC can be NAMED instead of guessed.
    //
    // Deliberately raw: it stores evidence and reconstructs offline. This
    // project has twice built a probe around an assumed shape and measured
    // nothing (the Virus HOSTPARK, the one-frame-depth caller). A ring has no
    // shape to be wrong about.
    //
    // Cost: 12 stores per instruction into a fixed array. No time calls, no
    // printf, no memory reads - per-access diagnostics that read memory or
    // take timestamps have slowed this class of emulator enough to change
    // its behaviour. Opcode bytes are fetched at DUMP time, not per step.
    // ---------------------------------------------------------------------
    static constexpr uint32_t PCRING_SIZE = 64;   // power of two
    struct PcRingEntry {
        uint32_t pc;
        uint32_t sp;
        uint32_t er[8];
        uint16_t ccr;
        uint16_t exr;
        uint64_t seq;
    };
    PcRingEntry m_pcring[PCRING_SIZE] = {};
    uint64_t    m_pcring_seq = 0;
    bool        m_pc_offmap_reported = false;   // TOOL-PCOFFMAP (BUG82): one report per run
    // BUG72: this used to be ONE global latch ("one derail is the interesting one").
    // In practice the first dump of a run is the early-boot [SP-GAP], and it then
    // SWALLOWED every later dump - including the MS2K_LOOPWATCH ring a whole round
    // was aimed at, which printed nothing and looked exactly like "the site is never
    // reached". A budget shared between an early bulk event and the rare event it was
    // armed for is spent on the early one; this thread has paid for that rule four
    // times. One dump PER REASON now, so each instrument gets its own.
    const char* m_pcring_reasons[8] = {nullptr};
    int         m_pcring_reason_count = 0;
    void pcRingDump(const char* reason);

    // ---------------------------------------------------------------------
    // FRAME RING (2026-09-13). Every push24 / pop24 - the two call-frame
    // primitives every JSR/BSR/RTS/TRAPA goes through - with the PC and the SP
    // either side. Dumped next to the PC ring on a derail.
    //
    // Built for STACK-0x1CE6E: the stack is exactly ONE WORD high at the failing
    // RTS, and the PC ring only shows the last 64 instructions, by which point
    // the unmatched push or pop is long gone. A frame ring covers thousands of
    // instructions in the same 64 slots because frames are rare.
    //
    // Records the VALUE too: a push of a return address and the pop that should
    // return it must match, and if they do not, the pairing itself is the answer.
    // ---------------------------------------------------------------------
    static constexpr uint32_t FRAMERING_SIZE = 64;   // power of two
    struct FrameRingEntry {
        uint32_t pc;        // PC at the time
        uint32_t sp_before;
        uint32_t sp_after;
        uint32_t value;     // pushed or popped
        uint64_t seq;
        bool     is_push;
    };
    FrameRingEntry m_framering[FRAMERING_SIZE] = {};
    uint64_t       m_framering_seq = 0;
    void frameRingRecord(bool is_push, uint32_t value, uint32_t sp_before, uint32_t sp_after);
    // Where the PREVIOUS frame operation left SP. If the next one does not find it
    // there, something moved the stack pointer outside push24/pop24 - which is the
    // whole question in STACK-0x1CE6E. 0xFFFFFFFF = not set yet.
    uint32_t m_last_frame_sp = 0xFFFFFFFFu;
    bool     m_sp_gap_reported = false;
    void frameRingDump();

    // k4.txt: Assisted-VBR mechanism
    bool m_assist_vbr_enabled;
    
    // k5.txt: Dev illegal guard
    bool m_dev_illegal_guard_enabled;
    int m_illegal_skip_count;
    
    // k8.txt: VBR candidate page tracking
    uint32_t m_vecpage_candidate;

    // fw4.txt: SCI kick-start mechanism
    uint64_t m_last_sci_tx_time_ns;
    uint64_t m_kick_start_threshold_ns;
    bool m_kick_start_injected;

    // Stack alignment statistics (v2.txt improvement #2)
    uint32_t m_stack_alignment_errors;
    uint32_t m_stack_alignment_corrections;

    // PC history ring buffer for odd-PC debug (last 10 entries)
    struct PcHistoryEntry {
        uint32_t pc;
        uint16_t opcode;
        uint8_t size;
    };
    PcHistoryEntry m_pc_history[10];
    int m_pc_history_idx = 0;

    // Instruction execution
    void executeInstruction();
    void executeInstructionFast();   // PERF-MCU-2
    bool m_spUnderflowReported = false;
    void handleInterrupt(H8S2350Interrupt interrupt);
    void updateFlags();
    
    // Address translation
    uint32_t translateAddress(uint32_t address) const;
    
    // Interrupt handling (updated based on MAME)
    void updateIRQFilter();
    void interruptTaken();
    bool exrInStack() const;

    // ------------------------------------------------------------------------
    // THE EXCEPTION FRAME, in one place. Two RENDERED figures that agree:
    //   RENDERED page  99 (printed 63), Figure 2.16   (c) and (d)
    //   RENDERED page 124 (printed 88), Figure 4.5(2) (a) and (b)
    // ADVANCED mode - which is the only mode this machine runs in:
    //   interrupt control mode 0:  SP+0 CCR | SP+1..3 PC(24)          4 bytes
    //   interrupt control mode 2:  SP+0 EXR | +1 Reserved (ignored on
    //                              return) | +2 CCR | +3..5 PC(24)    6 bytes
    // The mode is SYSCR's INTM1, which exrInStack() reads (BUG91).
    // pushExceptionFrame(), executeRTE(), peekStackedPC24() and the self-tests
    // ALL derive their layout from here - nobody re-spells it. BUG98.
    // ------------------------------------------------------------------------
    uint32_t excFrameSize()      const { return exrInStack() ? 6u : 4u; }
    uint32_t excFrameCcrOffset() const { return exrInStack() ? 2u : 0u; }
    uint32_t excFramePcOffset()  const { return excFrameCcrOffset() + 1u; }
    bool     excFrameHasExr()    const { return exrInStack(); }
    
    // Debug helpers
    void logInstruction(const std::string& instruction);
    void logMemoryAccess(uint32_t address, uint32_t value, bool isWrite);
    void dumpMemoryMap() const;

    // Replay Debugger interface
public:
    bool startRecording(const std::string& filename);
    void stopRecording();
    bool startReplay(const std::string& filename);
    void stopReplay();
    bool isRecording() const { return m_record_mode && m_replay_logger && m_replay_logger->isOpen(); }
    bool isReplaying() const { return m_replay_mode && m_replay_logger && m_replay_logger->isOpen(); }
    void syncReplay(); // Call from main loop to sync events

    // Flash ROM access
    // BUG102: a CPU store into CS0 is a FLASH BUS CYCLE (command state machine), never an
    // array write. Self-tests that plant opcodes/vectors in the flash window are a
    // PROGRAMMER, not the CPU: they open this scope, and inside it stores land in the
    // array directly. Nothing in a normal boot opens it.
    struct FlashHarnessScope {
        H8S2350Emulator& e;
        explicit FlashHarnessScope(H8S2350Emulator& em) : e(em) { ++e.m_flash_harness_depth; }
        ~FlashHarnessScope() { --e.m_flash_harness_depth; }
    };
    int m_flash_harness_depth = 0;
    void flashBusCycle(uint32_t address, uint16_t data, int width);   // width 1 or 2
    FlashROM& getFlashROM() { return m_flash_rom; }
    const FlashROM& getFlashROM() const { return m_flash_rom; }

private:
    bool m_full_io_trace_enabled;
    std::vector<std::string> m_io_trace_log;
    
    // Vector Table Tracker - Runtime RAM Vector Table Detection System
    std::unique_ptr<VectorTableTracker> m_vector_tracker;
    
    // Advanced Stack Corruption Detection System - Absolute Address Taint Tracking
    struct TaintSlot {
        uint32_t returnAddresses[3];  // Three bytes of the return address
        uint32_t pushedPC;            // PC value that was pushed
        uint32_t pushPC;              // PC that performed the push
        uint64_t cycleTime;           // When it was pushed
        bool active;                  // True if this slot is active
    };
    
    struct AdvancedStackTaint {
        std::vector<TaintSlot> raTaints;       // Active return address slots
        bool trackingEnabled = true;
        uint64_t currentCycle = 0;
        
        void addReturnAddress(uint32_t addr1, uint32_t addr2, uint32_t addr3, 
                             uint32_t pushedPC, uint32_t pushPC, uint64_t cycle) {
            TaintSlot slot;
            slot.returnAddresses[0] = addr1;
            slot.returnAddresses[1] = addr2; 
            slot.returnAddresses[2] = addr3;
            slot.pushedPC = pushedPC;
            slot.pushPC = pushPC;
            slot.cycleTime = cycle;
            slot.active = true;
            raTaints.push_back(slot);
        }
        
        void removeReturnAddress(uint32_t addr1, uint32_t addr2, uint32_t addr3) {
            for (auto& slot : raTaints) {
                if (slot.active && 
                    slot.returnAddresses[0] == addr1 &&
                    slot.returnAddresses[1] == addr2 &&
                    slot.returnAddresses[2] == addr3) {
                    slot.active = false;
                    break;
                }
            }
        }
        
        const TaintSlot* findTaintSlot(uint32_t addr) const {
            for (const auto& slot : raTaints) {
                if (slot.active) {
                    for (int i = 0; i < 3; i++) {
                        if (slot.returnAddresses[i] == addr) {
                            return &slot;
                        }
                    }
                }
            }
            return nullptr;
        }
        
        bool isInRedZone(uint32_t addr, int zoneSize = 16) const {
            for (const auto& slot : raTaints) {
                if (slot.active) {
                    for (int i = 0; i < 3; i++) {
                        uint32_t raAddr = slot.returnAddresses[i];
                        if (addr >= (raAddr - zoneSize) && addr <= (raAddr + zoneSize)) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }
    };
    
    AdvancedStackTaint m_stackTaint;

    // Debug: late PC write watch (for shift/rotate smoke diagnostics)
public:
#if defined(DEBUG)
    void enablePCWriteWatch(const char* tag) { m_pc_write_watch_active = true; m_pc_write_watch_tag = tag ? tag : ""; }
    void disablePCWriteWatch() { m_pc_write_watch_active = false; m_pc_write_watch_tag.clear(); }
    bool m_pc_write_watch_active = false;
    std::string m_pc_write_watch_tag;
#endif

    // PC fuse to enforce decoded_pc+size through to next fetch
public:
    struct PcFuse {
        bool     active = false;
        uint32_t want   = 0;
        uint8_t  op     = 0;
        void arm(uint8_t op_, uint32_t want_) { active = true; op = op_; want = want_; }
        void disarm() { active = false; }
    };
    void armPCFuse(uint8_t op, uint32_t want) { m_pc_fuse.arm(op, pcMask24(want)); }
    void disarmPCFuse() { m_pc_fuse.disarm(); }
    // Debug aid: control whether stack canary failure halts execution (default: warn-only)
    void setStackCanaryEnforce(bool enforce) { m_canary_enforce = enforce; }
    bool isStackCanaryEnforced() const { return m_canary_enforce; }
private:
    DebugLast m_dbg_last;
    PcFuse m_pc_fuse;
    LastExec m_last_exec;
    bool m_canary_enforce = false; // Off by default to avoid Debug degradation

    // ===== OPCODE COVERAGE AUDIT =====
    std::vector<uint64_t> m_opcode_hit_count;
public:
    const std::vector<uint64_t>& opcodeHits() const { return m_opcode_hit_count; }   // PERF-MCU: DIFFREF_BENCH histogram
private:
    std::vector<uint64_t> m_opcode_miss_count;
};

} // namespace MS2000
