#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <functional>

namespace MS2000 {

// Interrupt Vector Table Entry
struct InterruptVector {
    uint32_t handler_address;    // Address of interrupt handler
    uint8_t priority;           // Priority level (0-7, 7=highest)
    bool enabled;               // Interrupt enabled/disabled
    std::string description;    // Human-readable description
};

// Interrupt Context (saved registers during interrupt)
struct InterruptContext {
    uint32_t pc;               // Program Counter
    uint32_t sp;               // Stack Pointer
    uint16_t ccr;              // Condition Code Register
    uint16_t exr;              // Extended Register
    uint32_t er[8];            // General purpose registers ER0-ER7
    uint32_t cycles;           // Cycle count when interrupt occurred
};

// Interrupt Sources for H8S2350
enum class H8S2350InterruptSource {
    // System Interrupts
    RESET = 0,
    NMI = 1,
    
    // External Interrupts
    IRQ0 = 2,
    IRQ1 = 3,
    IRQ2 = 4,
    IRQ3 = 5,
    IRQ4 = 6,
    IRQ5 = 7,
    IRQ6 = 8,
    IRQ7 = 9,
    
    // Timer Interrupts
    WOVI = 10,      // Watchdog overflow
    CMI = 11,       // Compare match
    IMIA0 = 12,     // Timer channel 0 input capture A
    IMIB0 = 13,     // Timer channel 0 input capture B
    OVI0 = 14,      // Timer channel 0 overflow
    IMIA1 = 15,     // Timer channel 1 input capture A
    IMIB1 = 16,     // Timer channel 1 input capture B
    OVI1 = 17,      // Timer channel 1 overflow
    IMIA2 = 18,     // Timer channel 2 input capture A
    IMIB2 = 19,     // Timer channel 2 input capture B
    OVI2 = 20,      // Timer channel 2 overflow
    IMIA3 = 21,     // Timer channel 3 input capture A
    IMIB3 = 22,     // Timer channel 3 input capture B
    OVI3 = 23,      // Timer channel 3 overflow
    IMIA4 = 24,     // Timer channel 4 input capture A
    IMIB4 = 25,     // Timer channel 4 input capture B
    OVI4 = 26,      // Timer channel 4 overflow
    IMIA5 = 27,     // Timer channel 5 input capture A
    IMIB5 = 28,     // Timer channel 5 input capture B
    OVI5 = 29,      // Timer channel 5 overflow
    
    // k2.txt: HEW official vector numbers for H8S/2350
    ERI0 = 80,      // SCI channel 0 receive error
    RXI0 = 81,      // SCI channel 0 receive
    TXI0 = 82,      // SCI channel 0 transmit
    TEI0 = 83,      // SCI channel 0 transmit end
    ERI1 = 84,      // SCI channel 1 receive error
    RXI1 = 85,      // SCI channel 1 receive
    TXI1 = 86,      // SCI channel 1 transmit
    TEI1 = 87,      // SCI channel 1 transmit end
    // k2.txt: H8S/2350 has no SCI2 (88-91 reserved)
    
    // A/D Converter Interrupts
    ADI = 38,       // A/D conversion end
    
    // DMA Interrupts
    DMACI0 = 39,    // DMA channel 0
    DMACI1 = 40,    // DMA channel 1
    DMACI2 = 41,    // DMA channel 2
    DMACI3 = 42,    // DMA channel 3
    
    // MS2000 Specific Interrupts
    MIDI_RX = 43,   // MIDI receive
    MIDI_TX = 44,   // MIDI transmit
    KNOB_CHANGE = 45, // Knob position change
    BUTTON_PRESS = 46, // Button press
    LCD_UPDATE = 47,   // LCD update required
    
    // Reserved for future use
    RESERVED_48 = 48,
    RESERVED_49 = 49,
    RESERVED_50 = 50,
    RESERVED_51 = 51,
    RESERVED_52 = 52,
    RESERVED_53 = 53,
    RESERVED_54 = 54,
    RESERVED_55 = 55,
    RESERVED_56 = 56,
    RESERVED_57 = 57,
    RESERVED_58 = 58,
    RESERVED_59 = 59,
    RESERVED_60 = 60,
    RESERVED_61 = 61,
    RESERVED_62 = 62,
    RESERVED_63 = 63,
    
    MAX_INTERRUPTS = 64
};

// Interrupt Priority Levels
enum class InterruptPriority {
    LEVEL_0 = 0,    // Lowest priority
    LEVEL_1 = 1,
    LEVEL_2 = 2,
    LEVEL_3 = 3,
    LEVEL_4 = 4,
    LEVEL_5 = 5,
    LEVEL_6 = 6,
    LEVEL_7 = 7     // Highest priority
};

// Interrupt System Configuration
struct InterruptSystemConfig {
    uint32_t vector_table_base;     // Base address of interrupt vector table
    bool nested_interrupts;         // Allow nested interrupts
    uint8_t max_nesting_level;      // Maximum nesting level
    bool auto_priority_assignment;  // Automatically assign priorities
    bool debug_interrupts;          // Enable interrupt debugging
};

// Enhanced Interrupt System Class
class H8S2350InterruptSystem {
private:
    // Interrupt vector table (256 entries for H8S2350)
    std::array<InterruptVector, 256> m_vector_table;
    
    // Interrupt state
    std::array<bool, 256> m_interrupt_pending;
    std::array<bool, 256> m_interrupt_enabled;
    std::array<uint8_t, 256> m_interrupt_priority;
    
    // Interrupt context stack for nested interrupts
    std::vector<InterruptContext> m_context_stack;
    
    // Configuration
    InterruptSystemConfig m_config;
    
    // Statistics
    std::array<uint32_t, 256> m_interrupt_count;
    uint32_t m_total_interrupts;
    uint32_t m_nested_interrupts;
    
    // Helper functions
    void initializeVectorTable();
    void setDefaultPriorities();
    void setDefaultHandlers();
    InterruptContext saveContext();
    void restoreContext(const InterruptContext& context);
    uint32_t calculateVectorAddress(H8S2350InterruptSource source);
    bool isInterruptValid(H8S2350InterruptSource source);
    void logInterrupt(H8S2350InterruptSource source, const std::string& action);

public:
    H8S2350InterruptSystem();
    ~H8S2350InterruptSystem();
    
    // Initialization and configuration
    bool initialize(const InterruptSystemConfig& config);
    void reset();
    void configure(const InterruptSystemConfig& config);
    
    // Interrupt management
    void enableInterrupt(H8S2350InterruptSource source);
    void disableInterrupt(H8S2350InterruptSource source);
    void setInterruptPriority(H8S2350InterruptSource source, InterruptPriority priority);
    void triggerInterrupt(H8S2350InterruptSource source);
    void clearInterrupt(H8S2350InterruptSource source);
    
    // Interrupt handling
    H8S2350InterruptSource checkPendingInterrupts();
    bool serviceInterrupt(H8S2350InterruptSource source);
    void returnFromInterrupt();
    
    // Vector table management
    void setInterruptHandler(H8S2350InterruptSource source, uint32_t handler_address);
    void setInterruptDescription(H8S2350InterruptSource source, const std::string& description);
    uint32_t getInterruptHandler(H8S2350InterruptSource source);
    
    // Status queries
    bool isInterruptPending(H8S2350InterruptSource source);
    bool isInterruptEnabled(H8S2350InterruptSource source);
    uint8_t getInterruptPriority(H8S2350InterruptSource source);
    uint32_t getInterruptCount(H8S2350InterruptSource source);
    uint32_t getTotalInterrupts() const { return m_total_interrupts; }
    uint32_t getNestedInterrupts() const { return m_nested_interrupts; }
    
    // Debug and diagnostics
    void dumpVectorTable();
    void dumpInterruptStatistics();
    void resetStatistics();
    std::string getInterruptDescription(H8S2350InterruptSource source);
    
    // Memory interface (for vector table access)
    uint32_t readVectorTable(uint32_t address);
    void writeVectorTable(uint32_t address, uint32_t value);
    
    // Context management
    InterruptContext getCurrentContext();
    void setCurrentContext(const InterruptContext& context);
    size_t getContextStackDepth() const { return m_context_stack.size(); }
};

} // namespace MS2000
