#include "ms2000_lcd_emulator.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace MS2000 {

MS2000LCDEmulator::MS2000LCDEmulator() 
    : m_initialized(false)
    , m_busy(false)
    , m_display_on(true)
    , m_cursor_on(false)
    , m_cursor_blink(false)
    , m_cursor_x(0)
    , m_cursor_y(0)
    , m_ddram_address(0)
    , m_cgram_address(0)
    , m_entry_increment(true)
    , m_entry_shift(false)
    , m_blink_state(false)
    , m_blink_counter(0) {
    
    // Initialize control signals
    memset(&m_control, 0, sizeof(m_control));
    
    // Clear display buffer
    for (auto& line : m_display_buffer) {
        line.fill(' ');
    }
    
    // Clear pixel buffer
    for (auto& line : m_pixel_buffer) {
        line.fill(false);
    }
    
    // Initialize character set
    initializeCharacterSet();
    
    // Set up initial state
    clearDisplay();
    
    std::cout << "[LCD] MS2000 LCD Emulator initialized" << std::endl;
}

MS2000LCDEmulator::~MS2000LCDEmulator() {
}

void MS2000LCDEmulator::writeCommand(uint8_t command) {
    if (m_busy) return;
    
    std::cout << "[LCD] Command: 0x" << std::hex << std::setw(2) << std::setfill('0') 
              << (int)command << std::endl;
    
    if ((command & 0x80) != 0) {
        // Set DDRAM Address
        setDDRAMAddress(command & 0x7F);
    } else if ((command & 0x40) != 0) {
        // Set CGRAM Address
        setCGRAMAddress(command & 0x3F);
    } else if ((command & 0x20) != 0) {
        // Function Set
        setFunctionSet(command);
    } else if ((command & 0x10) != 0) {
        // Cursor/Display Shift
        // Implementation for cursor/display shifting
    } else if ((command & 0x08) != 0) {
        // Display Control
        setDisplayControl(command);
    } else if ((command & 0x04) != 0) {
        // Entry Mode Set
        setEntryMode(command);
    } else if (command == 0x02) {
        // Return Home
        returnHome();
    } else if (command == 0x01) {
        // Clear Display
        clearDisplay();
    }
    
    updatePixelBuffer();
    notifyDisplayUpdate();
}

void MS2000LCDEmulator::writeData(uint8_t data) {
    if (m_busy) return;
    
    if (m_cgram_address < 64) {
        // Writing to CGRAM (custom characters)
        int char_index = m_cgram_address / 8;
        int line_index = m_cgram_address % 8;
        
        if (char_index < 8) {
            m_custom_chars[char_index][line_index] = data;
        }
        
        m_cgram_address++;
    } else {
        // Writing to DDRAM (display)
        putChar(data);
        
        std::cout << "[LCD] Data: 0x" << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)data << " ('" << (char)data << "') at (" 
                  << (int)m_cursor_x << "," << (int)m_cursor_y << ")" << std::endl;
    }
    
    updatePixelBuffer();
    notifyDisplayUpdate();
}

uint8_t MS2000LCDEmulator::readStatus() {
    uint8_t status = 0;
    
    if (m_busy) {
        status |= static_cast<uint8_t>(LCDStatus::BUSY_FLAG);
    }
    
    status |= (m_ddram_address & static_cast<uint8_t>(LCDStatus::ADDRESS_COUNTER_MASK));
    
    return status;
}

uint8_t MS2000LCDEmulator::readData() {
    if (m_busy) return 0;
    
    uint8_t data = 0;
    
    if (m_cgram_address < 64) {
        // Reading from CGRAM
        int char_index = m_cgram_address / 8;
        int line_index = m_cgram_address % 8;
        
        if (char_index < 8) {
            data = m_custom_chars[char_index][line_index];
        }
        
        m_cgram_address++;
    } else {
        // Reading from DDRAM
        if (m_cursor_y < LCD_HEIGHT && m_cursor_x < LCD_WIDTH) {
            data = m_display_buffer[m_cursor_y][m_cursor_x];
        }
        
        moveCursor();
    }
    
    return data;
}

void MS2000LCDEmulator::initializeCharacterSet() {
    // Initialize standard ASCII character patterns
    // This is a simplified version - in reality would have full HD44780 character set
    
    // Clear all patterns
    for (auto& pattern : m_char_patterns) {
        pattern.fill(0);
    }
    
    // Define some basic ASCII characters (simplified 5x8 patterns)
    // Space (0x20)
    m_char_patterns[0x20] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    // 'A' (0x41)
    m_char_patterns[0x41] = {0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00};
    
    // 'B' (0x42)
    m_char_patterns[0x42] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00};
    
    // Continue for other characters as needed...
    // For simplicity, fill with basic patterns that show the character code
    for (int i = 0x20; i <= 0x7E; i++) {
        if (m_char_patterns[i][0] == 0) {
            // Create a simple pattern based on character code
            for (int line = 0; line < 8; line++) {
                m_char_patterns[i][line] = (i + line) & 0x1F;
            }
        }
    }
    
    // Define MS2000 custom characters
    defineCustomChar(0, CustomChars::NOTE);
    defineCustomChar(1, CustomChars::ARROW_LEFT);
    defineCustomChar(2, CustomChars::ARROW_RIGHT);
    defineCustomChar(3, CustomChars::ARROW_UP);
    defineCustomChar(4, CustomChars::ARROW_DOWN);
    defineCustomChar(5, CustomChars::BAR_EMPTY);
    defineCustomChar(6, CustomChars::BAR_FULL);
    defineCustomChar(7, CustomChars::FILTER_SYMBOL);
}

void MS2000LCDEmulator::clearDisplay() {
    for (auto& line : m_display_buffer) {
        line.fill(' ');
    }
    
    m_cursor_x = 0;
    m_cursor_y = 0;
    m_ddram_address = 0;
    
    std::cout << "[LCD] Display cleared" << std::endl;
}

void MS2000LCDEmulator::returnHome() {
    m_cursor_x = 0;
    m_cursor_y = 0;
    m_ddram_address = 0;
    
    std::cout << "[LCD] Cursor returned home" << std::endl;
}

void MS2000LCDEmulator::setEntryMode(uint8_t mode) {
    m_entry_increment = (mode & 0x02) != 0;
    m_entry_shift = (mode & 0x01) != 0;
    
    std::cout << "[LCD] Entry mode: " << (m_entry_increment ? "increment" : "decrement")
              << ", " << (m_entry_shift ? "shift" : "no shift") << std::endl;
}

void MS2000LCDEmulator::setDisplayControl(uint8_t control) {
    m_display_on = (control & 0x04) != 0;
    m_cursor_on = (control & 0x02) != 0;
    m_cursor_blink = (control & 0x01) != 0;
    
    std::cout << "[LCD] Display: " << (m_display_on ? "on" : "off")
              << ", Cursor: " << (m_cursor_on ? "on" : "off")
              << ", Blink: " << (m_cursor_blink ? "on" : "off") << std::endl;
}

void MS2000LCDEmulator::setFunctionSet(uint8_t function) {
    bool data_8bit = (function & 0x10) != 0;
    bool two_lines = (function & 0x08) != 0;
    bool font_5x10 = (function & 0x04) != 0;
    
    std::cout << "[LCD] Function: " << (data_8bit ? "8-bit" : "4-bit")
              << ", " << (two_lines ? "2-line" : "1-line")
              << ", " << (font_5x10 ? "5x10" : "5x8") << " font" << std::endl;
    
    m_initialized = true;
}

void MS2000LCDEmulator::setCGRAMAddress(uint8_t address) {
    m_cgram_address = address & 0x3F;
    
    std::cout << "[LCD] CGRAM address set to: 0x" << std::hex << (int)m_cgram_address << std::endl;
}

void MS2000LCDEmulator::setDDRAMAddress(uint8_t address) {
    m_ddram_address = address & 0x7F;
    addressToCursor(m_ddram_address, m_cursor_x, m_cursor_y);
    
    std::cout << "[LCD] DDRAM address set to: 0x" << std::hex << (int)m_ddram_address
              << " -> (" << (int)m_cursor_x << "," << (int)m_cursor_y << ")" << std::endl;
}

void MS2000LCDEmulator::putChar(uint8_t character) {
    if (m_cursor_y < LCD_HEIGHT && m_cursor_x < LCD_WIDTH) {
        m_display_buffer[m_cursor_y][m_cursor_x] = character;
        
        // fw26.txt: Trigger DDRAM write callback for event-driven LCD validation
        if (m_ddram_callback) {
            m_ddram_callback(m_ddram_address, character);
        }
    }
    
    moveCursor();
}

void MS2000LCDEmulator::moveCursor() {
    if (m_entry_increment) {
        m_cursor_x++;
        if (m_cursor_x >= LCD_WIDTH) {
            m_cursor_x = 0;
            m_cursor_y++;
            if (m_cursor_y >= LCD_HEIGHT) {
                m_cursor_y = 0;
            }
        }
    } else {
        if (m_cursor_x > 0) {
            m_cursor_x--;
        } else {
            m_cursor_x = LCD_WIDTH - 1;
            if (m_cursor_y > 0) {
                m_cursor_y--;
            } else {
                m_cursor_y = LCD_HEIGHT - 1;
            }
        }
    }
    
    m_ddram_address = getDDRAMAddress(m_cursor_x, m_cursor_y);
}

uint8_t MS2000LCDEmulator::getDDRAMAddress(uint8_t x, uint8_t y) const {
    // HD44780 address mapping for 2-line display
    if (y == 0) {
        return x;
    } else if (y == 1) {
        return 0x40 + x;
    }
    return 0;
}

void MS2000LCDEmulator::addressToCursor(uint8_t address, uint8_t& x, uint8_t& y) const {
    if (address < 0x40) {
        x = address;
        y = 0;
    } else {
        x = address - 0x40;
        y = 1;
    }
    
    // Clamp to valid range
    if (x >= LCD_WIDTH) x = LCD_WIDTH - 1;
    if (y >= LCD_HEIGHT) y = LCD_HEIGHT - 1;
}

void MS2000LCDEmulator::defineCustomChar(uint8_t char_code, const std::array<uint8_t, 8>& pattern) {
    if (char_code < 8) {
        m_custom_chars[char_code] = pattern;
    }
}

void MS2000LCDEmulator::updatePixelBuffer() {
    // Clear pixel buffer
    for (auto& line : m_pixel_buffer) {
        line.fill(false);
    }
    
    if (!m_display_on) return;
    
    // Render each character
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            uint8_t char_code = m_display_buffer[y][x];
            int pixel_x = x * LCD_CHAR_WIDTH;
            int pixel_y = y * LCD_CHAR_HEIGHT;
            
            renderCharacter(char_code, pixel_x, pixel_y);
        }
    }
    
    // Render cursor if enabled
    if (m_cursor_on) {
        int cursor_pixel_x = m_cursor_x * LCD_CHAR_WIDTH;
        int cursor_pixel_y = m_cursor_y * LCD_CHAR_HEIGHT + 7; // Bottom line
        
        bool show_cursor = true;
        if (m_cursor_blink) {
            show_cursor = m_blink_state;
        }
        
        if (show_cursor) {
            for (int x = 0; x < LCD_CHAR_WIDTH; x++) {
                if (cursor_pixel_x + x < LCD_PIXEL_WIDTH && cursor_pixel_y < LCD_PIXEL_HEIGHT) {
                    m_pixel_buffer[cursor_pixel_y][cursor_pixel_x + x] = true;
                }
            }
        }
    }
}

void MS2000LCDEmulator::renderCharacter(uint8_t char_code, int pixel_x, int pixel_y) {
    const std::array<uint8_t, 8>* pattern = nullptr;
    
    // Choose pattern source
    if (char_code < 8) {
        // Custom character
        pattern = &m_custom_chars[char_code];
    } else {
        // Standard character
        pattern = &m_char_patterns[char_code];
    }
    
    // Render 5x8 character pattern
    for (int line = 0; line < 8; line++) {
        uint8_t line_pattern = (*pattern)[line];
        
        for (int bit = 0; bit < 5; bit++) {
            bool pixel_on = (line_pattern & (0x10 >> bit)) != 0;
            
            int px = pixel_x + bit;
            int py = pixel_y + line;
            
            if (px < LCD_PIXEL_WIDTH && py < LCD_PIXEL_HEIGHT) {
                m_pixel_buffer[py][px] = pixel_on;
            }
        }
    }
}

void MS2000LCDEmulator::notifyDisplayUpdate() {
    if (m_display_callback) {
        m_display_callback();
    }
}

std::string MS2000LCDEmulator::getDisplayLine(int line) const {
    if (line < 0 || line >= LCD_HEIGHT) {
        return "";
    }
    
    std::string result;
    for (int x = 0; x < LCD_WIDTH; x++) {
        uint8_t ch = m_display_buffer[line][x];
        if (ch >= 0x20 && ch <= 0x7E) {
            result += static_cast<char>(ch);
        } else {
            result += '?'; // Non-printable character
        }
    }
    
    return result;
}

std::string MS2000LCDEmulator::getFullDisplay() const {
    std::string result;
    for (int line = 0; line < LCD_HEIGHT; line++) {
        result += getDisplayLine(line);
        if (line < LCD_HEIGHT - 1) {
            result += "\n";
        }
    }
    return result;
}

// MS2000 specific display functions
void MS2000LCDEmulator::showPatchName(const std::string& name) {
    clearDisplay();
    
    // Center the patch name on first line
    std::string display_name = name;
    if (display_name.length() > LCD_WIDTH) {
        display_name = display_name.substr(0, LCD_WIDTH);
    }
    
    int start_pos = (LCD_WIDTH - display_name.length()) / 2;
    
    setDDRAMAddress(start_pos);
    for (char c : display_name) {
        writeData(static_cast<uint8_t>(c));
    }
    
    std::cout << "[LCD] Showing patch: " << name << std::endl;
}

void MS2000LCDEmulator::showParameterValue(const std::string& param, const std::string& value) {
    setDDRAMAddress(0x00); // First line
    
    std::string line1 = param;
    if (line1.length() > LCD_WIDTH) {
        line1 = line1.substr(0, LCD_WIDTH);
    }
    line1.resize(LCD_WIDTH, ' ');
    
    for (char c : line1) {
        writeData(static_cast<uint8_t>(c));
    }
    
    setDDRAMAddress(0x40); // Second line
    
    std::string line2 = value;
    if (line2.length() > LCD_WIDTH) {
        line2 = line2.substr(0, LCD_WIDTH);
    }
    line2.resize(LCD_WIDTH, ' ');
    
    for (char c : line2) {
        writeData(static_cast<uint8_t>(c));
    }
    
    std::cout << "[LCD] Parameter: " << param << " = " << value << std::endl;
}

void MS2000LCDEmulator::showInitMessage() {
    clearDisplay();
    
    setDDRAMAddress(0x00);
    for (char c : std::string(LCDMessages::STARTUP)) {
        writeData(static_cast<uint8_t>(c));
    }
    
    setDDRAMAddress(0x40);
    for (char c : std::string(LCDMessages::INITIALIZING)) {
        writeData(static_cast<uint8_t>(c));
    }
    
    std::cout << "[LCD] Showing init message" << std::endl;
}

void MS2000LCDEmulator::updateAnimation() {
    m_blink_counter++;
    if (m_blink_counter >= 500) { // Adjust timing as needed
        m_blink_state = !m_blink_state;
        m_blink_counter = 0;
        
        if (m_cursor_blink) {
            updatePixelBuffer();
            notifyDisplayUpdate();
        }
    }
}

// fw26.txt: DDRAM direct access for rewind functionality
uint8_t MS2000LCDEmulator::getDDRAMByte(uint8_t addr) const {
    uint8_t x, y;
    addressToCursor(addr, x, y);
    
    if (y < LCD_HEIGHT && x < LCD_WIDTH) {
        return m_display_buffer[y][x];
    }
    
    return ' '; // Return space for invalid addresses
}

} // namespace MS2000