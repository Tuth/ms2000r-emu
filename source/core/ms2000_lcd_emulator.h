#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace MS2000 {

// MS2000 LCD Specifications (NCC06005H02 compatible)
constexpr int LCD_WIDTH = 16;           // Characters per line
constexpr int LCD_HEIGHT = 2;           // Number of lines
constexpr int LCD_CHAR_WIDTH = 5;       // Character width in pixels
constexpr int LCD_CHAR_HEIGHT = 8;      // Character height in pixels
constexpr int LCD_PIXEL_WIDTH = LCD_WIDTH * LCD_CHAR_WIDTH;
constexpr int LCD_PIXEL_HEIGHT = LCD_HEIGHT * LCD_CHAR_HEIGHT;

// LCD Commands (HD44780 compatible)
enum class LCDCommand : uint8_t {
    CLEAR_DISPLAY = 0x01,
    RETURN_HOME = 0x02,
    ENTRY_MODE_SET = 0x04,
    DISPLAY_CONTROL = 0x08,
    CURSOR_SHIFT = 0x10,
    FUNCTION_SET = 0x20,
    SET_CGRAM_ADDR = 0x40,
    SET_DDRAM_ADDR = 0x80
};

// LCD Status flags
enum class LCDStatus : uint8_t {
    BUSY_FLAG = 0x80,
    ADDRESS_COUNTER_MASK = 0x7F
};

// LCD Control signals
struct LCDControl {
    bool enable;        // E (Enable)
    bool register_select; // RS (0=command, 1=data)
    bool read_write;    // R/W (0=write, 1=read)
};

class MS2000LCDEmulator {
public:
    MS2000LCDEmulator();
    ~MS2000LCDEmulator();
    
    // LCD Interface (H8S → LCD)
    void writeCommand(uint8_t command);
    void writeData(uint8_t data);
    uint8_t readStatus();
    uint8_t readData();
    
    // Control signals
    void setControl(const LCDControl& control);
    void pulse_enable();
    
    // Display state
    bool isInitialized() const { return m_initialized; }
    bool isBusy() const { return m_busy; }
    
    // Display content access
    std::string getDisplayLine(int line) const;
    std::string getFullDisplay() const;
    const std::array<std::array<uint8_t, LCD_WIDTH>, LCD_HEIGHT>& getDisplayBuffer() const { return m_display_buffer; }
    
    // fw26.txt: DDRAM direct access for rewind functionality
    uint8_t getDDRAMByte(uint8_t addr) const;
    uint8_t getCurrentDDRAMAddress() const { return m_ddram_address; }
    
    // Pixel-level access for GUI
    const std::array<std::array<bool, LCD_PIXEL_WIDTH>, LCD_PIXEL_HEIGHT>& getPixelBuffer() const { return m_pixel_buffer; }
    void updatePixelBuffer();
    
    // Custom character support
    void defineCustomChar(uint8_t char_code, const std::array<uint8_t, 8>& pattern);
    
    // Callbacks for display updates
    using DisplayUpdateCallback = std::function<void()>;
    void setDisplayUpdateCallback(DisplayUpdateCallback callback) { m_display_callback = callback; }
    
    // fw26.txt: DDRAM write callback for event-driven LCD validation
    using DDRAMWriteCallback = std::function<void(uint8_t addr, uint8_t data)>;
    void setDDRAMWriteCallback(DDRAMWriteCallback callback) { m_ddram_callback = callback; }
    
    // MS2000 specific features
    void showPatchName(const std::string& name);
    void showParameterValue(const std::string& param, const std::string& value);
    void showMenu(const std::string& menu_name);
    void showInitMessage();
    void showMIDIActivity();
    
    // Animation support
    void startCursorBlink();
    void stopCursorBlink();
    void updateAnimation();
    
private:
    // Display memory
    std::array<std::array<uint8_t, LCD_WIDTH>, LCD_HEIGHT> m_display_buffer;
    std::array<std::array<bool, LCD_PIXEL_WIDTH>, LCD_PIXEL_HEIGHT> m_pixel_buffer;
    
    // Character generator ROM/RAM
    std::array<std::array<uint8_t, 8>, 256> m_char_patterns;  // Character patterns
    std::array<std::array<uint8_t, 8>, 8> m_custom_chars;     // Custom characters (CGRAM)
    
    // LCD State
    bool m_initialized;
    bool m_busy;
    bool m_display_on;
    bool m_cursor_on;
    bool m_cursor_blink;
    
    // Addressing
    uint8_t m_cursor_x;
    uint8_t m_cursor_y;
    uint8_t m_ddram_address;
    uint8_t m_cgram_address;
    
    // Control state
    LCDControl m_control;
    bool m_entry_increment;
    bool m_entry_shift;
    
    // Animation state
    bool m_blink_state;
    uint32_t m_blink_counter;
    
    // Callbacks
    DisplayUpdateCallback m_display_callback;
    DDRAMWriteCallback m_ddram_callback;  // fw26.txt: DDRAM write notification
    
    // Internal methods
    void initializeCharacterSet();
    void clearDisplay();
    void returnHome();
    void setEntryMode(uint8_t mode);
    void setDisplayControl(uint8_t control);
    void setFunctionSet(uint8_t function);
    void setCGRAMAddress(uint8_t address);
    void setDDRAMAddress(uint8_t address);
    
    void putChar(uint8_t character);
    void moveCursor();
    void scrollDisplay();
    
    uint8_t getDDRAMAddress(uint8_t x, uint8_t y) const;
    void addressToCursor(uint8_t address, uint8_t& x, uint8_t& y) const;
    
    void renderCharacter(uint8_t char_code, int pixel_x, int pixel_y);
    void notifyDisplayUpdate();
    
    // MS2000 specific display patterns
    void showStartupLogo();
    void showParameterBar(int value, int max_value);
};

// MS2000 LCD Messages and Patterns
namespace LCDMessages {
    constexpr const char* STARTUP = "  KORG MS2000  ";
    constexpr const char* INITIALIZING = " Initializing.. ";
    constexpr const char* READY = "     Ready      ";
    constexpr const char* MIDI_IN = "MIDI IN        ";
    constexpr const char* PROGRAM_CHANGE = "Program Change ";
    constexpr const char* EDIT_MODE = "Edit Mode      ";
    constexpr const char* WRITE_MODE = "Write Mode     ";
}

// Character patterns for MS2000 custom symbols
namespace CustomChars {
    // Musical note symbol
    constexpr std::array<uint8_t, 8> NOTE = {0x02, 0x03, 0x02, 0x0E, 0x1E, 0x0C, 0x00, 0x00};
    
    // Arrow symbols
    constexpr std::array<uint8_t, 8> ARROW_LEFT = {0x00, 0x04, 0x0C, 0x1F, 0x0C, 0x04, 0x00, 0x00};
    constexpr std::array<uint8_t, 8> ARROW_RIGHT = {0x00, 0x04, 0x06, 0x1F, 0x06, 0x04, 0x00, 0x00};
    constexpr std::array<uint8_t, 8> ARROW_UP = {0x00, 0x04, 0x0E, 0x15, 0x04, 0x04, 0x00, 0x00};
    constexpr std::array<uint8_t, 8> ARROW_DOWN = {0x00, 0x04, 0x04, 0x15, 0x0E, 0x04, 0x00, 0x00};
    
    // Level bars
    constexpr std::array<uint8_t, 8> BAR_EMPTY = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x00};
    constexpr std::array<uint8_t, 8> BAR_FULL = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00};
    
    // Special MS2000 symbols
    constexpr std::array<uint8_t, 8> FILTER_SYMBOL = {0x01, 0x03, 0x07, 0x0F, 0x07, 0x03, 0x01, 0x00};
    constexpr std::array<uint8_t, 8> LFO_SYMBOL = {0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E, 0x00, 0x00};
}

} // namespace MS2000