#pragma once

#include "real_lcd_display.h"
#include "h8s2350_emulator.h"

namespace MS2000 {

class H8SLCDAdapter {
public:
    H8SLCDAdapter(H8S2350Emulator& h8s, RealLCDDisplay& lcd);
    
    // Process LCD commands from H8S emulator
    void processLCDCommands();
    
    // Simulate pin-level signal routing between MCU and LCD
    void routePinSignals(uint8_t portF, uint8_t portG);
    
    // Execute LCD command through pin-level simulation
    void executeLCDCommandPinLevel(bool rs, bool rw, uint8_t data);
    
    // MP Block interface functions
    void processMPBlockInterface(uint8_t adBus, uint8_t selLine);
    void updateMPBlockToLCD();
    void updateLCDToMPBlock();
    void updateMPBlockToMCU();
    
    // Get the LCD display
    RealLCDDisplay& getLCD() { return m_lcd; }
    const RealLCDDisplay& getLCD() const { return m_lcd; }
    
    // Check if LCD content has changed
    bool hasLCDChanged() const { return m_lcd.hasChanged(); }
    void clearLCDChanged() { m_lcd.clearChanged(); }
    
    // Get I/O trace log from H8S emulator
    const std::vector<std::string>& getIOTraceLog() const { return m_h8s.getIOTraceLog(); }
    void clearIOTraceLog() { m_h8s.clearIOTraceLog(); }
    
    // Set cycle count from test application
    void setCycleCount(uint32_t cycle) { m_cycleCount = cycle; }

private:
    H8S2350Emulator& m_h8s;
    RealLCDDisplay& m_lcd;
    
    // H8S port addresses for LCD control (based on firmware analysis)
    static constexpr uint32_t LCD_PORT_F_ADDR = 0xFF0060;  // Port 60 - LCD control port (FF00xx) - PRIMARY
    static constexpr uint32_t LCD_PORT_G_ADDR = 0xFF0061;  // Port 61 - LCD data port (FF00xx) - PRIMARY
    static constexpr uint32_t LCD_PORT_F_ADDR_ALT = 0xFFFF60; // Alternate addressing (FFFFxx) - FALLBACK
    static constexpr uint32_t LCD_PORT_G_ADDR_ALT = 0xFFFF61; // Alternate addressing (FFFFxx) - FALLBACK
    
    // Alternative port addresses (if the above don't work)
    static constexpr uint32_t LCD_PORT_E_ADDR = 0xFFFF6E;  // Port 6E - alternative LCD control (FFFFxx)
    static constexpr uint32_t LCD_PORT_D_ADDR = 0xFFFF6A;  // Port 6A - alternative LCD data (FFFFxx)
    static constexpr uint32_t LCD_PORT_E_ADDR_ALT = 0xFF006E; // Alternate addressing (FF00xx)
    static constexpr uint32_t LCD_PORT_D_ADDR_ALT = 0xFF006A; // Alternate addressing (FF00xx)
    
    // Additional port addresses that might be used by MS2000
    static constexpr uint32_t LCD_PORT_1_ADDR = 0xFFFF60;  // Port 1 - possible LCD control
    static constexpr uint32_t LCD_PORT_2_ADDR = 0xFFFF61;  // Port 2 - possible LCD data
    static constexpr uint32_t LCD_PORT_3_ADDR = 0xFFFF62;  // Port 3 - possible LCD control
    static constexpr uint32_t LCD_PORT_4_ADDR = 0xFFFF63;  // Port 4 - possible LCD data
    static constexpr uint32_t LCD_PORT_5_ADDR = 0xFFFF64;  // Port 5 - possible LCD control
    static constexpr uint32_t LCD_PORT_6_ADDR = 0xFFFF65;  // Port 6 - possible LCD data
    static constexpr uint32_t LCD_PORT_7_ADDR = 0xFFFF66;  // Port 7 - possible LCD control
    static constexpr uint32_t LCD_PORT_8_ADDR = 0xFFFF67;  // Port 8 - possible LCD data
    
    // More port addresses to monitor (broader range)
    static constexpr uint32_t LCD_PORT_9_ADDR = 0xFFFF68;  // Port 9
    static constexpr uint32_t LCD_PORT_A_ADDR = 0xFFFF69;  // Port A
    static constexpr uint32_t LCD_PORT_B_ADDR = 0xFFFF6A;  // Port B
    static constexpr uint32_t LCD_PORT_C_ADDR = 0xFFFF6B;  // Port C
    
    // Alternative port ranges that might be used
    static constexpr uint32_t LCD_ALT_1_ADDR = 0xFF0000;  // Alternative range 1
    static constexpr uint32_t LCD_ALT_2_ADDR = 0xFF0001;  // Alternative range 2
    static constexpr uint32_t LCD_ALT_3_ADDR = 0xFF0002;  // Alternative range 3
    static constexpr uint32_t LCD_ALT_4_ADDR = 0xFF0003;  // Alternative range 4
    
    // LCD control bits (may vary depending on actual MS2000 wiring)
    static constexpr uint8_t LCD_RS_BIT = 0;  // Register Select
    static constexpr uint8_t LCD_RW_BIT = 1;  // Read/Write
    static constexpr uint8_t LCD_E_BIT = 2;   // Enable
    
    // Pin-level connection simulation
    struct LCDPinConnections {
        // MCU Port F pins (0xFF0060) - Control signals
        uint8_t mcu_pf0_rs;    // Register Select (RS) - Pin F0
        uint8_t mcu_pf1_rw;    // Read/Write (RW) - Pin F1  
        uint8_t mcu_pf2_en;    // Enable (E) - Pin F2
        uint8_t mcu_pf3_vcc;   // VCC - Pin F3
        uint8_t mcu_pf4_gnd;   // GND - Pin F4
        uint8_t mcu_pf5_nc;    // Not Connected - Pin F5
        uint8_t mcu_pf6_nc;    // Not Connected - Pin F6
        uint8_t mcu_pf7_nc;    // Not Connected - Pin F7
        
        // MCU Port G pins (0xFF0061) - Data bus
        uint8_t mcu_pg0_d0;    // Data bit 0 - Pin G0
        uint8_t mcu_pg1_d1;    // Data bit 1 - Pin G1
        uint8_t mcu_pg2_d2;    // Data bit 2 - Pin G2
        uint8_t mcu_pg3_d3;    // Data bit 3 - Pin G3
        uint8_t mcu_pg4_d4;    // Data bit 4 - Pin G4
        uint8_t mcu_pg5_d5;    // Data bit 5 - Pin G5
        uint8_t mcu_pg6_d6;    // Data bit 6 - Pin G6
        uint8_t mcu_pg7_d7;    // Data bit 7 - Pin G7
        
        // LCD side connections
        uint8_t lcd_rs;        // LCD Register Select pin
        uint8_t lcd_rw;        // LCD Read/Write pin
        uint8_t lcd_en;        // LCD Enable pin
        uint8_t lcd_d0;        // LCD Data bit 0
        uint8_t lcd_d1;        // LCD Data bit 1
        uint8_t lcd_d2;        // LCD Data bit 2
        uint8_t lcd_d3;        // LCD Data bit 3
        uint8_t lcd_d4;        // LCD Data bit 4
        uint8_t lcd_d5;        // LCD Data bit 5
        uint8_t lcd_d6;        // LCD Data bit 6
        uint8_t lcd_d7;        // LCD Data bit 7
        uint8_t lcd_vcc;       // LCD VCC
        uint8_t lcd_gnd;       // LCD GND
        
        // Busy flag and status
        uint8_t lcd_busy;      // LCD Busy flag
        uint8_t lcd_ready;     // LCD Ready signal
    };
    
    LCDPinConnections m_pins;
    
    // MP Block interface (Microprocessor/Multiplexer block)
    // This simulates the actual MS2000 architecture where MCU → MP Block → LCD
    struct MPBlockInterface {
        // AD[0-7] bus - 8-bit data/address bus from MCU to MP block
        uint8_t ad_bus;        // AD[0-7] data/address bus
        
        // Select line - MCU selects the MP block
        uint8_t sel_line;      // Select line for MP block
        
        // MP block internal state
        uint8_t mp_data_reg;   // MP block data register
        uint8_t mp_control_reg; // MP block control register
        uint8_t mp_status_reg; // MP block status register
        
        // MP block to LCD connections (actual LCD pin signals)
        uint8_t lcd_rs;        // LCD Register Select (from MP block)
        uint8_t lcd_rw;        // LCD Read/Write (from MP block)
        uint8_t lcd_en;        // LCD Enable (from MP block)
        uint8_t lcd_d0;        // LCD Data bit 0 (from MP block)
        uint8_t lcd_d1;        // LCD Data bit 1 (from MP block)
        uint8_t lcd_d2;        // LCD Data bit 2 (from MP block)
        uint8_t lcd_d3;        // LCD Data bit 3 (from MP block)
        uint8_t lcd_d4;        // LCD Data bit 4 (from MP block)
        uint8_t lcd_d5;        // LCD Data bit 5 (from MP block)
        uint8_t lcd_d6;        // LCD Data bit 6 (from MP block)
        uint8_t lcd_d7;        // LCD Data bit 7 (from MP block)
        
        // LCD response back to MP block
        uint8_t lcd_busy;      // LCD Busy flag (to MP block)
        uint8_t lcd_data_out;  // LCD data output (to MP block)
        
        // MP block response back to MCU
        uint8_t mp_data_out;   // MP block data output (to MCU AD bus)
        uint8_t mp_ready;      // MP block ready signal (to MCU)
    };
    
    MPBlockInterface m_mpBlock;
    
    uint8_t m_lastPortF = 0;
    uint8_t m_lastPortG = 0;
    uint8_t m_lastPortE = 0;
    uint8_t m_lastPortD = 0;
    uint8_t m_lastPort1 = 0;
    uint8_t m_lastPort2 = 0;
    uint8_t m_lastPort3 = 0;
    uint8_t m_lastPort4 = 0;
    uint8_t m_lastPort5 = 0;
    uint8_t m_lastPort6 = 0;
    uint8_t m_lastPort7 = 0;
    uint8_t m_lastPort8 = 0;
    bool m_lastEnable = false;
	
    // Mapping autodetect state
    int m_selectedMapping = -1; // -1 = unknown, 0..5 = permutations of bits (0,1,2)
    bool m_lastEnableByMap[6] = {false,false,false,false,false,false};
    
    // Timeout detection
    int m_cycleCount = 0;
    int m_lastActivityCycle = 0;
    static constexpr int TIMEOUT_CYCLES = 1000;  // 1000 cycles without activity
};

} // namespace MS2000
