#include "ms2k_lcd_adapter.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include "async_log.h"

namespace MS2000 { extern AsyncLogger g_log; }

// fw3.txt helper functions
uint32_t Ms2kLcdAdapter::now_ms() const {
    using clk = std::chrono::steady_clock;
    static auto t0 = clk::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(clk::now()-t0).count();
}

// fw4.txt: High-precision timing for HD44780 busy flag simulation
uint64_t Ms2kLcdAdapter::now_ns() const {
    using clk = std::chrono::steady_clock;
    static auto t0 = clk::now();
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now()-t0).count();
}

bool Ms2kLcdAdapter::isBusyReadContext() const {
    // Busy flag read: RS=0, RW=1, E=1
    return (!RS()) && RW_is_asserted() && E_is_asserted();
}

void Ms2kLcdAdapter::writePort(uint8_t port, uint8_t value) {
    // DEBUG: stderr trace for LCD ports
    if (port == 4 || port == 5) {
        fprintf(stderr, "[LCD-PORT] write port=%d val=0x%02X\n", port, value);
        fflush(stderr);
    }

    if (log) { 
        char b[64]; 
        snprintf(b,sizeof(b),"[GPIO-MONITOR] WRITE port=0x%02X value=0x%02X\n", port, value); 
        log(b); 
    }

    // Update latch based on which port is written
    if (port == 4) {  // Port 4: RS, RW, E, DB4
        updateLatchFromPort4(value);
        if (log) { 
            char b[128]; 
            snprintf(b,sizeof(b),"[LCD-ADAPTER] CTRL WRITE Port 4: value=0x%02X RS=%d RW=%d E=%d DB4=%d\n", 
                     value, (value>>4)&1, (value>>5)&1, (value>>6)&1, (value>>7)&1); 
            log(b); 
        }
    } else if (port == 5) {  // Port 5: DB5, DB6, DB7
        updateLatchFromPort5(value);
        if (log) { 
            char b[128]; 
            snprintf(b,sizeof(b),"[LCD-ADAPTER] DATA WRITE Port 5: value=0x%02X DB5=%d DB6=%d DB7=%d\n", 
                     value, (value>>0)&1, (value>>1)&1, (value>>2)&1); 
            log(b); 
        }
    }

    // GPIO trace (fw3.txt) - first 512 events
    if (trace.size() < 512) {
        trace.push_back({ now_ms(),'W',port,value,
            uint8_t(RS()), uint8_t(RW()), uint8_t(E_is_asserted()) });
    }

    // Process 4-bit transaction on E rising edge
    // We track E state changes to detect rising edge
}

uint8_t Ms2kLcdAdapter::readPort(uint8_t port) {
    // GPIO trace (fw3.txt) - first 512 events
    if (trace.size() < 512) {
        trace.push_back({ now_ms(),'R',port,0,
            uint8_t(RS()), uint8_t(RW()), uint8_t(E_is_asserted()) });
    }

    // fw3.txt monitoring: Log ALL port reads to find firmware LCD ports
    if (log) { 
        char b[64]; 
        snprintf(b,sizeof(b),"[GPIO-MONITOR] READ port=0x%02X RW=%d E=%d RS=%d\n", 
                 port, (int)RW_is_asserted(), (int)E_is_asserted(), (int)RS()); 
        log(b); 
    }

    // fw4.txt 250ms bootstrap sentinel - ANY read gets BF=0 to break firmware busy state
    static auto t0 = std::chrono::steady_clock::now();
    bool bootstrap = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count() < 10;

    if (bootstrap) {
        if (log) { 
            char b[64]; 
            snprintf(b,sizeof(b),"[LCD-ADAPTER] FW4 BOOTSTRAP: ANY read port=0x%02X -> BF=0 (extended power-up)\n", port); 
            log(b); 
        }
        return 0x00; // DB7=0 (busy flag clear) - break ANY busy state
    }

    // Handle reads from Port 4 (control) or Port 5 (data)
    if (port == 4) { // Port 4 read - control/status
        bool rs = RS();
        bool rw = RW_is_asserted();
        bool e  = E_is_asserted();

        // HD44780 busy flag read: RS=0, RW=1, E=1
        if (!rs && rw && e) {
            bf_read_count++;
            uint8_t status = 0x00;  // BF=0 (not busy), AC=0x00
            if (log) { 
                char b[64]; 
                snprintf(b,sizeof(b),"[LCD-ADAPTER] BF read on Port 4: status=0x%02X (BF=0)\n", status); 
                log(b); 
            }
            return status;
        }
        // Return control register value
        return ((RS() ? 1 : 0) << 4) | ((RW_is_asserted() ? 1 : 0) << 5) | ((E_is_asserted() ? 1 : 0) << 6) | (m_latch.db4 << 7);
    } else if (port == 5) { // Port 5 read - data
        // Return DB4-DB7 in bits 0-3 (DB4 comes from Port 4 bit 7)
        uint8_t data = 0;
        data |= (m_latch.db4 << 0); // DB4 from Port 4 bit 7
        data |= (m_latch.db5 << 1); // DB5 from Port 5 bit 0
        data |= (m_latch.db6 << 2); // DB6 from Port 5 bit 1
        data |= (m_latch.db7 << 3); // DB7 from Port 5 bit 2
        return data;
    }

    return 0x00;
}

void Ms2kLcdAdapter::writeAddress(uint32_t address, uint8_t value)
{
    // DEBUG: stderr trace for LCD ports
    if ((address & 0xFFFF00) == 0xFFFF00) {
        fprintf(stderr, "[LCD-ADDR] write addr=0x%08X val=0x%02X\n", address, value);
        fflush(stderr);
    }

    // Update auto-detection activity timestamp
    updateActivity();

    // Notify about GPIO activity for auto-detection
    if (onGPIOActivity) {
        onGPIOActivity();
    }

    // fw3.txt monitoring: Log ALL address writes to find firmware LCD addresses
    if (log) { char b[64]; snprintf(b,sizeof(b),"[GPIO-MONITOR] WRITE address=0x%06X value=0x%02X\n", address, value); log(b); }

    // Debug: Special logging for ANY writes to potential LCD ports
    if ((address & 0xFFFFFF00) == 0xFFFF60) {
        printf("[IO-WRITE] Firmware wrote to LCD I/O address=0x%06X value=0x%02X\n", address, value);
    }

    // Map actual MS2000 port addresses
    // Port 4 DR = 0xFF0060 (control + DB4) - short 0xFF60 -> full 0xFF0060
    // Port 5 DR = 0xFF0064 (DB5, DB6, DB7) - short 0xFF64 -> full 0xFF0064
    bool is_port4 = (address == 0xFF0060);
    bool is_port5 = (address == 0xFF0064);
    bool is_port4_ddr = (address == 0xFF0061);  // Port 4 DDR
    bool is_port5_ddr = (address == 0xFF0065);  // Port 5 DDR

    if (log) { char b[128]; snprintf(b,sizeof(b),"[LCD-ADAPTER-NEW] writeAddress: addr=0x%06X port4=%d port5=%d\n", address, is_port4, is_port5); log(b); }

    // Special debug for LCD writes
    if (is_port4 || is_port5 || is_port4_ddr || is_port5_ddr) {
        printf("[LCD-WRITE] Address=0x%06X Value=0x%02X - Firmware writing to LCD port!\n", address, value);
    }

    // Handle writes to Port 4 (control + DB4)
    if (is_port4) {
        uint16_t prevE = (m_latch.e ? 1 : 0);
        updateLatchFromPort4(value);
        bool rs = RS();
        bool rw = RW_is_asserted();
        bool e  = E_is_asserted();

        if (log) { 
            char b[128]; 
            snprintf(b,sizeof(b),"[LCD-ADAPTER-NEW] CTRL WRITE Port 4: value=0x%02X RS=%d RW=%d E=%d DB4=%d\n", 
                     value, rs, rw, e, (value>>7)&1); 
            log(b); 
        }

        // HD44780 E rising edge detection
        if (!prevE && e) {
            if (m_latchValid) {
                commit(rs, rw);
                m_latchValid = false;
            }
        }
    }

    // Handle DDR writes for Port 4
    if (is_port4_ddr) {
        if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] DDR WRITE Port 4: value=0x%02X\n", value); log(b); }
    }

    // Handle writes to Port 5 (DB5, DB6, DB7)
    if (is_port5) {
        updateLatchFromPort5(value);
        if (log) { 
            char b[128]; 
            snprintf(b,sizeof(b),"[LCD-ADAPTER] DATA WRITE Port 5: value=0x%02X DB5=%d DB6=%d DB7=%d\n", 
                     value, (value>>0)&1, (value>>1)&1, (value>>2)&1); 
            log(b); 
        }
    }

    // Handle DDR writes for Port 5
    if (is_port5_ddr) {
        if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] DDR WRITE Port 5: value=0x%02X\n", value); log(b); }
    }

    // GPIO trace (fw3.txt) - first 512 events
    if (trace.size() < 512) {
        trace.push_back({ now_ms(),'W',(uint8_t)(address & 0xFF),value,
            uint8_t(RS()), uint8_t(RW_is_asserted()), uint8_t(E_is_asserted())});
    }
}

uint8_t Ms2kLcdAdapter::readAddress(uint32_t address)
{
    // fw3.txt monitoring: Log ALL address reads to find firmware LCD addresses
    if (log) { char b[64]; snprintf(b,sizeof(b),"[GPIO-MONITOR] READ address=0x%06X RW=%d E=%d RS=%d\n",
      address, (int)RW_is_asserted(), (int)E_is_asserted(), (int)RS()); log(b); }

    // Map actual MS2000 port addresses to port identifiers
    // Port 4 DR = 0xFF0060, Port 5 DR = 0xFF0064
    uint8_t port_id = 0xFF;
    bool is_port4 = (address == 0xFF0060);
    bool is_port5 = (address == 0xFF0064);
    bool is_port4_read = (address == 0xFF0050);
    bool is_port5_read = (address == 0xFF0054);

    if (is_port4 || is_port4_read) port_id = 4;
    else if (is_port5 || is_port5_read) port_id = 5;

    // If not a known LCD port, use low byte
    if (port_id == 0xFF) {
        port_id = (uint8_t)(address & 0xFF);
    }

    // fw4.txt 250ms bootstrap sentinel - ANY read gets BF=0 to break firmware busy state
    static auto t0 = std::chrono::steady_clock::now();
    bool bootstrap = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count() < 10;

    if (bootstrap) {
        // During bootstrap, return BF=0 (ready) for ALL reads
        bf_read_count++;
        if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] BOOTSTRAP BF=0 (read #%d, port=0x%02X)\n", bf_read_count, port_id); log(b); }
        return 0x00; // BF=0 (not busy), AC=0x00
    }

    // GPIO trace (fw3.txt) - first 512 events
    if (trace.size() < 512) {
        trace.push_back({ now_ms(),'R',port_id,0,
            uint8_t(RS()), uint8_t(RW_is_asserted()), uint8_t(E_is_asserted())});
    }

    // Determine which port is being read
    bool is_port4_read_actual = is_port4 || is_port4_read;
    bool is_port5_read_actual = is_port5 || is_port5_read;

    bool rs = RS();
    bool rw = RW_is_asserted();
    bool e  = E_is_asserted();

    // HD44780 busy flag read: RS=0, RW=1, E=1 on status port
    if (is_port4_read_actual && !rs && rw && e) {
        bf_read_count++;
        uint8_t status = 0x00;  // BF=0 (not busy), AC=0x00
        if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] BF read on Port 4: status=0x%02X (BF=0)\n", status); log(b); }
        return status;
    }

    // Data reads from Port 5 when RS=1, RW=1
    if (is_port5_read_actual && rs && rw) {
        bf_read_count++;
        // Return DB4-DB7 in bits 0-3
        uint8_t data = 0;
        data |= (m_latch.db4 << 0);
        data |= (m_latch.db5 << 1);
        data |= (m_latch.db6 << 2);
        data |= (m_latch.db7 << 3);
        if (log) { char b[128]; snprintf(b,sizeof(b),"[LCD-ADAPTER] DATA READ from Port 5: 0x%02X (read #%d, RS=%d RW=%d E=%d)\n", data, bf_read_count, rs, rw, e); log(b); }
        lastReadValue = data;
        lastReadValid = true;
        return data;
    }

    // Control reads from Port 4
    if (is_port4_read_actual) {
        bf_read_count++;
        uint8_t ctrl = ((RS() ? 1 : 0) << 4) | ((RW_is_asserted() ? 1 : 0) << 5) | ((E_is_asserted() ? 1 : 0) << 6) | (m_latch.db4 << 7);
        if (log) { char b[128]; snprintf(b,sizeof(b),"[LCD-ADAPTER] CTRL READ from Port 4: ctrl=0x%02X (read #%d, RS=%d RW=%d E=%d)\n", ctrl, bf_read_count, rs, rw, e); log(b); }
        return ctrl;
    }

    // Default for other addresses
    return 0x00;
}

// fw3.txt autofix - DISABLED to prevent interference with busy flag timing
void Ms2kLcdAdapter::tryAutofix() {
    // Autofix disabled - busy flag timing must remain consistent
    // The firmware expects proper HD44780 busy flag behavior
    // Disabling E-gating breaks the busy flag timing expectations
}

// Auto-detection methods
void Ms2kLcdAdapter::setAutoDetectionMode(uint32_t timeout_ms) {
    m_detection_timeout_ms = timeout_ms;
    m_gpio_mode_active = true;
    m_last_gpio_activity = now_ms();
    if (log) {
        char b[128];
        snprintf(b, sizeof(b), "[LCD-ADAPTER] Auto-detection enabled: timeout=%u ms\n", timeout_ms);
        log(b);
    }
}

bool Ms2kLcdAdapter::isGpioModeActive() const {
    return m_gpio_mode_active;
}

void Ms2kLcdAdapter::updateActivity() {
    m_last_gpio_activity = now_ms();
}

bool Ms2kLcdAdapter::shouldSwitchToPanelMP() const {
    if (!m_gpio_mode_active) return false;
    uint32_t current_time = now_ms();
    uint32_t time_since_activity = current_time - m_last_gpio_activity;
    return time_since_activity > m_detection_timeout_ms;
}

// BUG44 - the Port 2 path. Everything the HD44780 needs arrives in one byte.
void Ms2kLcdAdapter::writePort2(uint8_t value)
{
    updateActivity();
    if (onGPIOActivity) onGPIOActivity();

    const bool prevE = m_latch.e;
    updateLatchFromPort2(value);
    const bool rs = m_latch.rs;
    const bool rw = m_latch.rw;
    const bool e  = m_latch.e;

    // HD44780: the controller takes the bus on the FALLING edge of E. Committing on
    // the rising edge happens to see the same data in the usual set-data / raise-E /
    // lower-E sequence, but it is not what the part does, and a firmware that changes
    // the data while E is high would be read wrongly. Fall, not rise.
    if (prevE && !e) {
        commit(rs, rw);
    }

    if (trace.size() < 512) {
        trace.push_back({ now_ms(), 'W', 0x61, value, uint8_t(rs), uint8_t(rw), uint8_t(e) });
    }
}

void Ms2kLcdAdapter::commit(bool rs, bool rw)
{
    if (!m_latchValid) return;
    
    // 4-bit mode: combine high nibble (already latched) with low nibble (to come next)
    // We track state: first E pulse = high nibble, second E pulse = low nibble + execute
    uint8_t highNibble = assembleHighNibble();
    
    if (!m_latch4bitState) {
        // First E pulse: received high nibble, store it
        m_latchedHighNibble = highNibble;
        m_latch4bitState = true;
        if (log) { 
            char b[64]; 
            snprintf(b,sizeof(b),"[LCD-ADAPTER] 4-bit: Got HIGH nibble 0x%01X, waiting for LOW nibble\n", highNibble); 
            log(b); 
        }
        return;
    }
    
    // BUG45, 2026-09-13 - EVERY ASSEMBLED BYTE WAS 0x00, AND THE LOG SAID SO.
    // assembleHighNibble() returns the four data bits ALREADY POSITIONED in bits
    // 7-4 (`db7<<7 | db6<<6 | db5<<5 | db4<<4`). The old line then did
    //     fullByte = (m_latchedHighNibble << 4) | (lowNibble & 0x0F)
    // which shifts a value already sitting in the top half four places further -
    // straight out of the byte - and masks bits 3-0 of a value that has nothing in
    // them. Both terms are always zero. Measured before the fix: 2,436 LCD command
    // writes and every single one of them 0x00.
    const uint8_t lowNibble = uint8_t((highNibble >> 4) & 0x0F);   // this pulse's nibble
    const uint8_t fullByte  = uint8_t((m_latchedHighNibble & 0xF0) | lowNibble);
    m_latch4bitState = false;
    m_latchValid = false;
    
    if (log) { 
        char b[64]; 
        snprintf(b,sizeof(b),"[LCD-ADAPTER] 4-bit: Combined byte 0x%02X (hi=0x%01X lo=0x%01X) RS=%d RW=%d\n", 
                 fullByte, (m_latchedHighNibble >> 4) & 0x0F, lowNibble & 0x0F, RS(), RW());
        log(b); 
    }
    
    if (rw) {
        // read cycle - just acknowledge
        lastReadValue = fullByte; 
        lastReadValid = true;
        if (log) log("[LCD-ADAPTER] READ cycle (BF=0)\n");
        return;
    }
    
    // fw4.txt: Set busy timing based on command type
    uint64_t current_ns = now_ns();
    uint64_t busy_duration_ns;
    
    if (!rs) {
        // Command
        if (fullByte == 0x01 || fullByte == 0x02) {
            // Clear Display (0x01) or Return Home (0x02) - 1.6ms
            busy_duration_ns = 1600000ULL;  // 1.6ms in nanoseconds
            if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] CMD  0x%02X (Clear/Home) - busy for 1.6ms\n", fullByte); log(b); }
        } else {
            // Other commands - 40µs default
            busy_duration_ns = 40000ULL;  // 40µs in nanoseconds
            if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] CMD  0x%02X - busy for 40µs\n", fullByte); log(b); }
        }
        
        busy_until_ns = current_ns + busy_duration_ns;
        if (onCmd) onCmd(fullByte);
    } else {
        // Data write
        busy_duration_ns = 40000ULL;  // 40µs in nanoseconds
        busy_until_ns = current_ns + busy_duration_ns;
        
        if (onData) onData(fullByte);
        if (log) { char b[64]; snprintf(b,sizeof(b),"[LCD-ADAPTER] DATA 0x%02X - busy for 40µs\n", fullByte); log(b); }
        lastReadValue = fullByte; 
        lastReadValid = true;
    }
}

// LCD Memory-Mapped Read Handler (for CS2/CS3 0x402000-0x402FFF)
// rs=false (RS=0): Read busy flag (bit7) + address counter [6:0]
// rs=true  (RS=1): Read DDRAM data
uint8_t Ms2kLcdAdapter::onReadLCD(bool rs) {
    if (rs) {
        // RS=1: Read DDRAM data
        if (lastReadValid) {
            uint8_t val = lastReadValue;
            lastReadValid = false;  // Consume
            if (log) { 
                char b[64]; 
                snprintf(b,sizeof(b),"[LCD-ADAPTER] DDRAM read: 0x%02X\n", val); 
                log(b); 
            }
            return val;
        }
        return 0x00;  // No data pending
    } else {
        // RS=0: Read busy flag (bit7) + address counter [6:0]
        // Busy if busy_until_ns > current time
        uint64_t current_ns = now_ns();
        uint8_t busy = (current_ns < busy_until_ns) ? 0x80 : 0x00;
        // Address counter - use a simple incrementing counter for now
        static uint8_t ac = 0;
        if (!busy) ac = (ac + 1) & 0x7F;  // Increment when not busy
        uint8_t status = busy | (ac & 0x7F);
        
        if (log && bf_read_count++ < 10) {
            char b[64];
            snprintf(b,sizeof(b),"[LCD-ADAPTER] Busy flag read: BF=%d AC=0x%02X\n", busy>>7, ac);
            log(b);
        }
        return status;
    }
}