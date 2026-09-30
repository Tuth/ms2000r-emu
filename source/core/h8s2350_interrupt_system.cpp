#include "h8s2350_interrupt_system.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <algorithm>

namespace MS2000 {

H8S2350InterruptSystem::H8S2350InterruptSystem()
    : m_total_interrupts(0)
    , m_nested_interrupts(0)
{
    // Initialize arrays
    m_interrupt_pending.fill(false);
    m_interrupt_enabled.fill(false);
    m_interrupt_priority.fill(0);
    m_interrupt_count.fill(0);
    
    // Initialize vector table
    for (auto& vector : m_vector_table) {
        vector.handler_address = 0;
        vector.priority = 0;
        vector.enabled = false;
        vector.description = "Unused";
    }
    
    // Set default configuration
    m_config.vector_table_base = 0x00000000;
    m_config.nested_interrupts = true;
    m_config.max_nesting_level = 8;
    m_config.auto_priority_assignment = true;
    m_config.debug_interrupts = false;
}

H8S2350InterruptSystem::~H8S2350InterruptSystem()
{
}

bool H8S2350InterruptSystem::initialize(const InterruptSystemConfig& config)
{
    std::cout << "🔧 Initializing H8S2350 Interrupt System..." << std::endl;
    
    m_config = config;
    
    // Initialize vector table
    initializeVectorTable();
    
    // Set default priorities
    setDefaultPriorities();
    
    // Set default handlers
    setDefaultHandlers();
    
    std::cout << "✅ Interrupt System initialized successfully" << std::endl;
    std::cout << "   Vector Table Base: 0x" << std::hex << m_config.vector_table_base << std::endl;
    std::cout << "   Nested Interrupts: " << (m_config.nested_interrupts ? "Enabled" : "Disabled") << std::endl;
    std::cout << "   Max Nesting Level: " << std::dec << static_cast<int>(m_config.max_nesting_level) << std::endl;
    
    return true;
}

void H8S2350InterruptSystem::reset()
{
    std::cout << "🔄 Resetting Interrupt System..." << std::endl;
    
    // Clear all pending interrupts
    m_interrupt_pending.fill(false);
    
    // Clear context stack
    m_context_stack.clear();
    
    // Reset statistics
    m_total_interrupts = 0;
    m_nested_interrupts = 0;
    m_interrupt_count.fill(0);
    
    std::cout << "✅ Interrupt System reset complete" << std::endl;
}

void H8S2350InterruptSystem::initializeVectorTable()
{
    // Set up default vector table entries
    std::vector<std::pair<H8S2350InterruptSource, std::string>> default_vectors = {
        {H8S2350InterruptSource::RESET, "System Reset"},
        {H8S2350InterruptSource::NMI, "Non-Maskable Interrupt"},
        {H8S2350InterruptSource::IRQ0, "External Interrupt 0"},
        {H8S2350InterruptSource::IRQ1, "External Interrupt 1"},
        {H8S2350InterruptSource::IRQ2, "External Interrupt 2"},
        {H8S2350InterruptSource::IRQ3, "External Interrupt 3"},
        {H8S2350InterruptSource::IRQ4, "External Interrupt 4"},
        {H8S2350InterruptSource::IRQ5, "External Interrupt 5"},
        {H8S2350InterruptSource::IRQ6, "External Interrupt 6"},
        {H8S2350InterruptSource::IRQ7, "External Interrupt 7"},
        {H8S2350InterruptSource::WOVI, "Watchdog Overflow"},
        {H8S2350InterruptSource::CMI, "Compare Match"},
        {H8S2350InterruptSource::IMIA0, "Timer 0 Input Capture A"},
        {H8S2350InterruptSource::IMIB0, "Timer 0 Input Capture B"},
        {H8S2350InterruptSource::OVI0, "Timer 0 Overflow"},
        {H8S2350InterruptSource::IMIA1, "Timer 1 Input Capture A"},
        {H8S2350InterruptSource::IMIB1, "Timer 1 Input Capture B"},
        {H8S2350InterruptSource::OVI1, "Timer 1 Overflow"},
        {H8S2350InterruptSource::IMIA2, "Timer 2 Input Capture A"},
        {H8S2350InterruptSource::IMIB2, "Timer 2 Input Capture B"},
        {H8S2350InterruptSource::OVI2, "Timer 2 Overflow"},
        {H8S2350InterruptSource::IMIA3, "Timer 3 Input Capture A"},
        {H8S2350InterruptSource::IMIB3, "Timer 3 Input Capture B"},
        {H8S2350InterruptSource::OVI3, "Timer 3 Overflow"},
        {H8S2350InterruptSource::IMIA4, "Timer 4 Input Capture A"},
        {H8S2350InterruptSource::IMIB4, "Timer 4 Input Capture B"},
        {H8S2350InterruptSource::OVI4, "Timer 4 Overflow"},
        {H8S2350InterruptSource::IMIA5, "Timer 5 Input Capture A"},
        {H8S2350InterruptSource::IMIB5, "Timer 5 Input Capture B"},
        {H8S2350InterruptSource::OVI5, "Timer 5 Overflow"},
        {H8S2350InterruptSource::ERI0, "SCI0 Receive Error"},
        {H8S2350InterruptSource::RXI0, "SCI0 Receive"},
        {H8S2350InterruptSource::TXI0, "SCI0 Transmit"},
        {H8S2350InterruptSource::TEI0, "SCI0 Transmit End"},
        {H8S2350InterruptSource::ERI1, "SCI1 Receive Error"},
        {H8S2350InterruptSource::RXI1, "SCI1 Receive"},
        {H8S2350InterruptSource::TXI1, "SCI1 Transmit"},
        {H8S2350InterruptSource::TEI1, "SCI1 Transmit End"},
        {H8S2350InterruptSource::ADI, "A/D Conversion End"},
        {H8S2350InterruptSource::DMACI0, "DMA Channel 0"},
        {H8S2350InterruptSource::DMACI1, "DMA Channel 1"},
        {H8S2350InterruptSource::DMACI2, "DMA Channel 2"},
        {H8S2350InterruptSource::DMACI3, "DMA Channel 3"},
        {H8S2350InterruptSource::MIDI_RX, "MIDI Receive"},
        {H8S2350InterruptSource::MIDI_TX, "MIDI Transmit"},
        {H8S2350InterruptSource::KNOB_CHANGE, "Knob Position Change"},
        {H8S2350InterruptSource::BUTTON_PRESS, "Button Press"},
        {H8S2350InterruptSource::LCD_UPDATE, "LCD Update Required"}
    };
    
    for (const auto& [source, description] : default_vectors) {
        size_t index = static_cast<size_t>(source);
        if (index < m_vector_table.size()) {
            m_vector_table[index].description = description;
            m_vector_table[index].enabled = false;
            m_vector_table[index].handler_address = 0;
            m_vector_table[index].priority = 0;
        }
    }
}

void H8S2350InterruptSystem::setDefaultPriorities()
{
    // Set default priorities based on H8S2350 specifications
    std::vector<std::pair<H8S2350InterruptSource, uint8_t>> default_priorities = {
        {H8S2350InterruptSource::RESET, 7},      // Highest priority
        {H8S2350InterruptSource::NMI, 6},
        {H8S2350InterruptSource::IRQ0, 5},
        {H8S2350InterruptSource::IRQ1, 5},
        {H8S2350InterruptSource::IRQ2, 4},
        {H8S2350InterruptSource::IRQ3, 4},
        {H8S2350InterruptSource::IRQ4, 3},
        {H8S2350InterruptSource::IRQ5, 3},
        {H8S2350InterruptSource::IRQ6, 2},
        {H8S2350InterruptSource::IRQ7, 2},
        {H8S2350InterruptSource::WOVI, 6},
        {H8S2350InterruptSource::CMI, 5},
        {H8S2350InterruptSource::IMIA0, 4},
        {H8S2350InterruptSource::IMIB0, 4},
        {H8S2350InterruptSource::OVI0, 3},
        {H8S2350InterruptSource::IMIA1, 4},
        {H8S2350InterruptSource::IMIB1, 4},
        {H8S2350InterruptSource::OVI1, 3},
        {H8S2350InterruptSource::IMIA2, 4},
        {H8S2350InterruptSource::IMIB2, 4},
        {H8S2350InterruptSource::OVI2, 3},
        {H8S2350InterruptSource::IMIA3, 4},
        {H8S2350InterruptSource::IMIB3, 4},
        {H8S2350InterruptSource::OVI3, 3},
        {H8S2350InterruptSource::IMIA4, 4},
        {H8S2350InterruptSource::IMIB4, 4},
        {H8S2350InterruptSource::OVI4, 3},
        {H8S2350InterruptSource::IMIA5, 4},
        {H8S2350InterruptSource::IMIB5, 4},
        {H8S2350InterruptSource::OVI5, 3},
        {H8S2350InterruptSource::ERI0, 3},
        {H8S2350InterruptSource::RXI0, 4},
        {H8S2350InterruptSource::TXI0, 2},
        {H8S2350InterruptSource::TEI0, 2},
        {H8S2350InterruptSource::ERI1, 3},
        {H8S2350InterruptSource::RXI1, 4},
        {H8S2350InterruptSource::TXI1, 2},
        {H8S2350InterruptSource::TEI1, 2},
        {H8S2350InterruptSource::ADI, 3},
        {H8S2350InterruptSource::DMACI0, 3},
        {H8S2350InterruptSource::DMACI1, 3},
        {H8S2350InterruptSource::DMACI2, 3},
        {H8S2350InterruptSource::DMACI3, 3},
        {H8S2350InterruptSource::MIDI_RX, 4},
        {H8S2350InterruptSource::MIDI_TX, 2},
        {H8S2350InterruptSource::KNOB_CHANGE, 1},
        {H8S2350InterruptSource::BUTTON_PRESS, 2},
        {H8S2350InterruptSource::LCD_UPDATE, 1}
    };
    
    for (const auto& [source, priority] : default_priorities) {
        setInterruptPriority(source, static_cast<InterruptPriority>(priority));
    }
}

void H8S2350InterruptSystem::setDefaultHandlers()
{
    // Set default handler addresses (these would typically be set by firmware)
    // For now, we'll use placeholder addresses
    std::vector<std::pair<H8S2350InterruptSource, uint32_t>> default_handlers = {
        {H8S2350InterruptSource::RESET, 0x00001000},
        {H8S2350InterruptSource::NMI, 0x00001004},
        {H8S2350InterruptSource::IRQ0, 0x00001008},
        {H8S2350InterruptSource::IRQ1, 0x0000100C},
        {H8S2350InterruptSource::IRQ2, 0x00001010},
        {H8S2350InterruptSource::IRQ3, 0x00001014},
        {H8S2350InterruptSource::IRQ4, 0x00001018},
        {H8S2350InterruptSource::IRQ5, 0x0000101C},
        {H8S2350InterruptSource::IRQ6, 0x00001020},
        {H8S2350InterruptSource::IRQ7, 0x00001024},
        {H8S2350InterruptSource::WOVI, 0x00001028},
        {H8S2350InterruptSource::CMI, 0x0000102C},
        {H8S2350InterruptSource::IMIA0, 0x00001030},
        {H8S2350InterruptSource::IMIB0, 0x00001034},
        {H8S2350InterruptSource::OVI0, 0x00001038},
        {H8S2350InterruptSource::IMIA1, 0x0000103C},
        {H8S2350InterruptSource::IMIB1, 0x00001040},
        {H8S2350InterruptSource::OVI1, 0x00001044},
        {H8S2350InterruptSource::IMIA2, 0x00001048},
        {H8S2350InterruptSource::IMIB2, 0x0000104C},
        {H8S2350InterruptSource::OVI2, 0x00001050},
        {H8S2350InterruptSource::IMIA3, 0x00001054},
        {H8S2350InterruptSource::IMIB3, 0x00001058},
        {H8S2350InterruptSource::OVI3, 0x0000105C},
        {H8S2350InterruptSource::IMIA4, 0x00001060},
        {H8S2350InterruptSource::IMIB4, 0x00001064},
        {H8S2350InterruptSource::OVI4, 0x00001068},
        {H8S2350InterruptSource::IMIA5, 0x0000106C},
        {H8S2350InterruptSource::IMIB5, 0x00001070},
        {H8S2350InterruptSource::OVI5, 0x00001074},
        {H8S2350InterruptSource::ERI0, 0x00001078},
        {H8S2350InterruptSource::RXI0, 0x0000107C},
        {H8S2350InterruptSource::TXI0, 0x00001080},
        {H8S2350InterruptSource::TEI0, 0x00001084},
        {H8S2350InterruptSource::ERI1, 0x00001088},
        {H8S2350InterruptSource::RXI1, 0x0000108C},
        {H8S2350InterruptSource::TXI1, 0x00001090},
        {H8S2350InterruptSource::TEI1, 0x00001094},
        {H8S2350InterruptSource::ADI, 0x00001098},
        {H8S2350InterruptSource::DMACI0, 0x0000109C},
        {H8S2350InterruptSource::DMACI1, 0x000010A0},
        {H8S2350InterruptSource::DMACI2, 0x000010A4},
        {H8S2350InterruptSource::DMACI3, 0x000010A8},
        {H8S2350InterruptSource::MIDI_RX, 0x000010AC},
        {H8S2350InterruptSource::MIDI_TX, 0x000010B0},
        {H8S2350InterruptSource::KNOB_CHANGE, 0x000010B4},
        {H8S2350InterruptSource::BUTTON_PRESS, 0x000010B8},
        {H8S2350InterruptSource::LCD_UPDATE, 0x000010BC}
    };
    
    for (const auto& [source, handler] : default_handlers) {
        setInterruptHandler(source, handler);
    }
}

void H8S2350InterruptSystem::enableInterrupt(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_interrupt_enabled.size()) {
        m_interrupt_enabled[index] = true;
        m_vector_table[index].enabled = true;
        logInterrupt(source, "Enabled");
    }
}

void H8S2350InterruptSystem::disableInterrupt(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_interrupt_enabled.size()) {
        m_interrupt_enabled[index] = false;
        m_vector_table[index].enabled = false;
        logInterrupt(source, "Disabled");
    }
}

void H8S2350InterruptSystem::setInterruptPriority(H8S2350InterruptSource source, InterruptPriority priority)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_interrupt_priority.size()) {
        m_interrupt_priority[index] = static_cast<uint8_t>(priority);
        m_vector_table[index].priority = static_cast<uint8_t>(priority);
        logInterrupt(source, "Priority set to " + std::to_string(static_cast<int>(priority)));
    }
}

void H8S2350InterruptSystem::triggerInterrupt(H8S2350InterruptSource source)
{
    if (!isInterruptValid(source)) {
        return;
    }
    
    size_t index = static_cast<size_t>(source);
    if (m_interrupt_enabled[index]) {
        m_interrupt_pending[index] = true;
        logInterrupt(source, "Triggered");
    }
}

void H8S2350InterruptSystem::clearInterrupt(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_interrupt_pending.size()) {
        m_interrupt_pending[index] = false;
        logInterrupt(source, "Cleared");
    }
}

H8S2350InterruptSource H8S2350InterruptSystem::checkPendingInterrupts()
{
    // Find highest priority pending interrupt
    H8S2350InterruptSource highest_interrupt = H8S2350InterruptSource::MAX_INTERRUPTS;
    uint8_t highest_priority = 0;
    
    for (size_t i = 0; i < static_cast<size_t>(H8S2350InterruptSource::MAX_INTERRUPTS); i++) {
        if (m_interrupt_pending[i] && m_interrupt_enabled[i]) {
            if (m_interrupt_priority[i] > highest_priority) {
                highest_priority = m_interrupt_priority[i];
                highest_interrupt = static_cast<H8S2350InterruptSource>(i);
            }
        }
    }
    
    return highest_interrupt;
}

bool H8S2350InterruptSystem::serviceInterrupt(H8S2350InterruptSource source)
{
    if (!isInterruptValid(source)) {
        return false;
    }
    
    size_t index = static_cast<size_t>(source);
    
    // Check if interrupt is pending and enabled
    if (!m_interrupt_pending[index] || !m_interrupt_enabled[index]) {
        return false;
    }
    
    // Check nesting level
    if (m_context_stack.size() >= m_config.max_nesting_level) {
        logInterrupt(source, "Nesting level exceeded, ignoring");
        return false;
    }
    
    // Save current context
    InterruptContext context = saveContext();
    m_context_stack.push_back(context);
    
    // Clear pending flag
    m_interrupt_pending[index] = false;
    
    // Update statistics
    m_interrupt_count[index]++;
    m_total_interrupts++;
    if (m_context_stack.size() > 1) {
        m_nested_interrupts++;
    }
    
    logInterrupt(source, "Servicing");
    
    return true;
}

void H8S2350InterruptSystem::returnFromInterrupt()
{
    if (m_context_stack.empty()) {
        std::cerr << "⚠️  No interrupt context to restore!" << std::endl;
        return;
    }
    
    // Restore context
    InterruptContext context = m_context_stack.back();
    m_context_stack.pop_back();
    restoreContext(context);
    
    if (m_config.debug_interrupts) {
        std::cout << "🔄 Returned from interrupt, context stack depth: " 
                  << m_context_stack.size() << std::endl;
    }
}

void H8S2350InterruptSystem::setInterruptHandler(H8S2350InterruptSource source, uint32_t handler_address)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_vector_table.size()) {
        m_vector_table[index].handler_address = handler_address;
        logInterrupt(source, "Handler set to 0x" + std::to_string(handler_address));
    }
}

void H8S2350InterruptSystem::setInterruptDescription(H8S2350InterruptSource source, const std::string& description)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_vector_table.size()) {
        m_vector_table[index].description = description;
    }
}

uint32_t H8S2350InterruptSystem::getInterruptHandler(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_vector_table.size()) {
        return m_vector_table[index].handler_address;
    }
    return 0;
}

bool H8S2350InterruptSystem::isInterruptPending(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    return (index < m_interrupt_pending.size()) && m_interrupt_pending[index];
}

bool H8S2350InterruptSystem::isInterruptEnabled(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    return (index < m_interrupt_enabled.size()) && m_interrupt_enabled[index];
}

uint8_t H8S2350InterruptSystem::getInterruptPriority(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_interrupt_priority.size()) {
        return m_interrupt_priority[index];
    }
    return 0;
}

uint32_t H8S2350InterruptSystem::getInterruptCount(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_interrupt_count.size()) {
        return m_interrupt_count[index];
    }
    return 0;
}

void H8S2350InterruptSystem::dumpVectorTable()
{
    std::cout << "\n📋 Interrupt Vector Table" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << std::setw(4) << "Vec" << " | " 
              << std::setw(8) << "Handler" << " | " 
              << std::setw(1) << "P" << " | " 
              << std::setw(1) << "E" << " | " 
              << std::setw(1) << "P" << " | " 
              << "Description" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    
    for (size_t i = 0; i < 64; i++) { // Show first 64 vectors
        const auto& vector = m_vector_table[i];
        std::cout << std::setw(4) << std::hex << i << " | " 
                  << std::setw(8) << std::hex << vector.handler_address << " | " 
                  << std::setw(1) << std::dec << static_cast<int>(vector.priority) << " | " 
                  << std::setw(1) << (vector.enabled ? "Y" : "N") << " | " 
                  << std::setw(1) << (m_interrupt_pending[i] ? "Y" : "N") << " | " 
                  << vector.description << std::endl;
    }
}

void H8S2350InterruptSystem::dumpInterruptStatistics()
{
    std::cout << "\n📊 Interrupt Statistics" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Interrupts: " << m_total_interrupts << std::endl;
    std::cout << "Nested Interrupts: " << m_nested_interrupts << std::endl;
    std::cout << "Current Context Stack Depth: " << m_context_stack.size() << std::endl;
    std::cout << std::endl;
    
    std::cout << "Interrupt Counts:" << std::endl;
    std::cout << std::string(40, '-') << std::endl;
    
    for (size_t i = 0; i < static_cast<size_t>(H8S2350InterruptSource::MAX_INTERRUPTS); i++) {
        if (m_interrupt_count[i] > 0) {
            std::cout << std::setw(20) << m_vector_table[i].description << ": " 
                      << m_interrupt_count[i] << std::endl;
        }
    }
}

void H8S2350InterruptSystem::resetStatistics()
{
    m_total_interrupts = 0;
    m_nested_interrupts = 0;
    m_interrupt_count.fill(0);
    std::cout << "📊 Interrupt statistics reset" << std::endl;
}

std::string H8S2350InterruptSystem::getInterruptDescription(H8S2350InterruptSource source)
{
    size_t index = static_cast<size_t>(source);
    if (index < m_vector_table.size()) {
        return m_vector_table[index].description;
    }
    return "Unknown";
}

uint32_t H8S2350InterruptSystem::readVectorTable(uint32_t address)
{
    uint32_t offset = address - m_config.vector_table_base;
    if (offset < m_vector_table.size() * sizeof(InterruptVector)) {
        size_t index = offset / sizeof(InterruptVector);
        size_t field = (offset % sizeof(InterruptVector)) / sizeof(uint32_t);
        
        switch (field) {
            case 0: return m_vector_table[index].handler_address;
            case 1: return (m_vector_table[index].priority << 24) | 
                         (m_vector_table[index].enabled ? 0x80000000 : 0);
            default: return 0;
        }
    }
    return 0;
}

void H8S2350InterruptSystem::writeVectorTable(uint32_t address, uint32_t value)
{
    uint32_t offset = address - m_config.vector_table_base;
    if (offset < m_vector_table.size() * sizeof(InterruptVector)) {
        size_t index = offset / sizeof(InterruptVector);
        size_t field = (offset % sizeof(InterruptVector)) / sizeof(uint32_t);
        
        switch (field) {
            case 0:
                m_vector_table[index].handler_address = value;
                break;
            case 1:
                m_vector_table[index].priority = (value >> 24) & 0xFF;
                m_vector_table[index].enabled = (value & 0x80000000) != 0;
                m_interrupt_enabled[index] = m_vector_table[index].enabled;
                m_interrupt_priority[index] = m_vector_table[index].priority;
                break;
        }
    }
}

InterruptContext H8S2350InterruptSystem::getCurrentContext()
{
    // This would be called by the emulator to get current context
    // For now, return a placeholder context
    InterruptContext context = {};
    return context;
}

void H8S2350InterruptSystem::setCurrentContext(const InterruptContext& context)
{
    // This would be called by the emulator to set current context
    // For now, just log it
    if (m_config.debug_interrupts) {
        std::cout << "🔄 Context updated: PC=0x" << std::hex << context.pc 
                  << ", SP=0x" << context.sp << std::endl;
    }
}

InterruptContext H8S2350InterruptSystem::saveContext()
{
    // This would save the current CPU state
    // For now, return a placeholder context
    InterruptContext context = {};
    return context;
}

void H8S2350InterruptSystem::restoreContext(const InterruptContext& context)
{
    // This would restore the CPU state
    // For now, just log it
    if (m_config.debug_interrupts) {
        std::cout << "🔄 Context restored: PC=0x" << std::hex << context.pc 
                  << ", SP=0x" << context.sp << std::endl;
    }
}

uint32_t H8S2350InterruptSystem::calculateVectorAddress(H8S2350InterruptSource source)
{
    return m_config.vector_table_base + (static_cast<uint32_t>(source) * 4);
}

bool H8S2350InterruptSystem::isInterruptValid(H8S2350InterruptSource source)
{
    return static_cast<size_t>(source) < static_cast<size_t>(H8S2350InterruptSource::MAX_INTERRUPTS);
}

void H8S2350InterruptSystem::logInterrupt(H8S2350InterruptSource source, const std::string& action)
{
    if (m_config.debug_interrupts) {
        size_t index = static_cast<size_t>(source);
        std::cout << "🔔 Interrupt " << static_cast<int>(source) 
                  << " (" << m_vector_table[index].description << "): " 
                  << action << std::endl;
    }
}

} // namespace MS2000
