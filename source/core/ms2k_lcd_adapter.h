#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <chrono>

// Forward declaration for MS2000MPStub
namespace MS2000 {
class MS2000MPStub;
}

enum class LcdStatusMode : uint8_t { Auto, BusByte, SingleBit };

// New pin configuration for split-port 4-bit mode (MS2000 schematic)
// Port 4 (0xFFFF60): P40-P43 unused, P44=RS, P45=R/W, P46=E, P47=DB4
// Port 5 (0xFFFF64): P50=DB5, P51=DB6, P52=DB7, P53-P57 unused
// DB0-DB3: Not connected (4-bit mode)
struct Ms2kLcdPins {
    // Hardware addresses
    uint32_t ctrlPortAddr = 0xFFFF60;    // Port 4 DR address
    uint32_t dataPortAddr = 0xFFFF64;    // Port 5 DR address
    
    // Port 4 bit positions
    uint8_t rsBit = 4;   // P44
    uint8_t rwBit = 5;   // P45
    uint8_t eBit = 6;    // P46
    uint8_t db4Bit = 7;  // P47
    
    // Port 5 bit positions
    uint8_t db5Bit = 0;  // P50
    uint8_t db6Bit = 1;  // P51
    uint8_t db7Bit = 2;  // P52
    
    // Optional: for busy flag detection
    LcdStatusMode statusMode = LcdStatusMode::Auto;
    uint32_t statusPortAddr = 0xFFFF64; // Port 5 for DB7 read
    bool statusRequireE = true;
};

class Ms2kLcdAdapter {
public:
    // Kimenet: standard HD44780 bus hívások (már megvan nálad)
    std::function<void(uint8_t)> onCmd;   // hd44780::writeCmd
    std::function<void(uint8_t)> onData;  // hd44780::writeData
    std::function<uint8_t(bool)> onRead;  // LCD read: RS=false -> busy flag+AC, RS=true -> DDRAM

    // (opcionális) diagnosztika
    std::function<void(const char*)> log;

    // Callback for GPIO activity notification
    std::function<void()> onGPIOActivity;

    // Set MP stub reference for auto-detection
    void setMPStub(MS2000::MS2000MPStub* mp_stub) { m_mp_stub = mp_stub; }

    explicit Ms2kLcdAdapter(const Ms2kLcdPins& pins)
        : P(pins), m_mp_stub(nullptr) { trace.reserve(512); initLatch(); }

    // H8S GPIO WRITE események ide jönnek:
    void writePort(uint8_t port, uint8_t value);

    // H8S GPIO READ – ha a FW RW=1 ciklust csinál és a dataPortot olvassa
    // (RW=1 módban a FW tipikusan inputtá teszi a portot és olvas)
    uint8_t readPort(uint8_t port);

    // Memory-mapped I/O methods for direct address handling
    void writeAddress(uint32_t address, uint8_t value);
    uint8_t readAddress(uint32_t address);

    // BUG44: the real entry point. One write to P2DR carries the whole HD44780
    // interface - four data bits plus E, RW and RS - so the transaction is driven
    // entirely from here. See updateLatchFromPort2() for the Tier-1 pin mapping.
    void writePort2(uint8_t value);

    // (opcionális) explicit reset
    void reset() { initLatch(); }

    // új diagnosztika: hányszor olvastak BF-et
    uint32_t bfReads() const { return bf_read_count; }
    
    // LCD Memory-Mapped Read (for CS2/CS3 0x402000-0x402FFF)
    // rs=false (RS=0): Read busy flag (bit7) + address counter [6:0]
    // rs=true  (RS=1): Read DDRAM data
    uint8_t onReadLCD(bool rs);

    // fw3.txt autofix - 300ms után fallback
    void tryAutofix();

    // Auto-detection between GPIO and Panel-MP paths
    void setAutoDetectionMode(uint32_t timeout_ms = 300000); // 300ms in microseconds
    bool isGpioModeActive() const;
    void updateActivity();
    bool shouldSwitchToPanelMP() const;

private:
    Ms2kLcdPins P;
    
    // Latched values from split ports
    struct LatchedSignals {
        bool rs = false;
        bool rw = false;
        bool e = false;
        uint8_t db4 = 0;
        uint8_t db5 = 0;
        uint8_t db6 = 0;
        uint8_t db7 = 0;
    };
    LatchedSignals m_latch;
    bool m_latchValid = false;
    
    // 4-bit mode state tracking
    bool m_latch4bitState = false;      // false = waiting for high nibble, true = waiting for low nibble
    uint8_t m_latchedHighNibble = 0;    // Stored high nibble from first E pulse
    
    uint8_t lastReadValue = 0x00;
    bool lastReadValid = false;
    
    // autodetect state
    bool statusLocked = false;
    uint32_t bf_read_count = 0;
    uint32_t rw1_reads_per_port[256] = {};   // olvasásszámláló portonként RW=1 alatt
    uint8_t last_read_port = 0xFF;
    
    // GPIO trace (fw3.txt)
    struct GpioTraceEv { uint32_t t_ms; char op; uint8_t port, val; uint8_t rs:1,rw:1,e:1; };
    std::vector<GpioTraceEv> trace;
    
    // Auto-detection state
    uint32_t m_last_gpio_activity;
    bool m_gpio_mode_active;
    uint32_t m_detection_timeout_ms;
    
    // Reference to MP stub for auto-detection
    MS2000::MS2000MPStub* m_mp_stub;
    
    // fw4.txt: HD44780 busy flag timing structure
    uint64_t busy_until_ns = 0;  // When LCD will be ready (nanoseconds)

    // Helper methods
    void initLatch() { m_latch = {}; m_latchValid = false; m_latch4bitState = false; m_latchedHighNibble = 0; }
    // BUG44, 2026-09-13 - THE ONLY MAPPING THAT IS TIER-1.
    // KORG service manual page 14, schematic KOD-A30411, read off the pin column
    // and the net labels together (they are seven rows, in this order):
    //
    //   LCD_DB4 -> RA9-D  -> pin 79 = P20      bit 0
    //   LCD_DB5 -> RA9-C  -> pin 78 = P21      bit 1
    //   LCD_DB6 -> RA9-B  -> pin 77 = P22      bit 2
    //   LCD_DB7 -> RA9-A  -> pin 76 = P23      bit 3
    //   LCD_E   -> RA11-D -> pin 75 = P24      bit 4
    //   LCD_RW  -> RA11-C -> pin 74 = P25      bit 5
    //   LCD_RS  -> RA11-B -> pin 73 = P26      bit 6
    //             RA11-A  -> pin 72 = P27      NC  (the schematic says so in words)
    //
    // ONE PORT. P2DR is 0xFFFF61, which this tree's I/O switch sees as 0xFF61.
    // Everything else in this file - "Port 4 DR = 0xFF0060 (control + DB4)",
    // "Port 5 DR = 0xFF0064 (DB5, DB6, DB7)" - and the 0xFFFC00/0xFFF400 pins the
    // emulator constructed this adapter with are inventions from an "I/O scan",
    // and 0xFFFC00/0xFFF400 are RAM addresses, not ports at all.
    void updateLatchFromPort2(uint8_t p2) {
        m_latch.db4 = (p2 >> 0) & 1;
        m_latch.db5 = (p2 >> 1) & 1;
        m_latch.db6 = (p2 >> 2) & 1;
        m_latch.db7 = (p2 >> 3) & 1;
        m_latch.e   = (p2 >> 4) & 1;
        m_latch.rw  = (p2 >> 5) & 1;
        m_latch.rs  = (p2 >> 6) & 1;
        m_latchValid = true;
    }

    void updateLatchFromPort4(uint8_t port4_val) {
        m_latch.rs = (port4_val >> 4) & 1;
        m_latch.rw = (port4_val >> 5) & 1;
        m_latch.e  = (port4_val >> 6) & 1;
        m_latch.db4 = (port4_val >> 7) & 1;
        m_latchValid = true;
    }
    void updateLatchFromPort5(uint8_t port5_val) {
        m_latch.db5 = (port5_val >> 0) & 1;
        m_latch.db6 = (port5_val >> 1) & 1;
        m_latch.db7 = (port5_val >> 2) & 1;
        m_latchValid = true;
    }
    
    bool RS() const { return m_latch.rs; }
    bool RW() const { return m_latch.rw; }
    bool E()  const { return m_latch.e; }
    
    uint8_t assembleHighNibble() const {
        // Combine DB4-DB7 from latch into high nibble (bits 7-4)
        // Correct bit positions: DB7=bit7, DB6=bit6, DB5=bit5, DB4=bit4
        return (m_latch.db7 << 7) | (m_latch.db6 << 6) | (m_latch.db5 << 5) | (m_latch.db4 << 4);
    }

    // Track if Port 5 (DB5-DB7) has been updated since last E pulse
    bool m_port5_updated = false;

    // E-edge counter for debug
    int m_e_edge_count = 0;

    void commit(bool rs, bool rw);
    bool RW_is_asserted() const { return m_latch.rw; }
    bool E_is_asserted() const { return m_latch.e; }
    bool isBusyReadContext() const;
    uint32_t now_ms() const;
    uint64_t now_ns() const;
};