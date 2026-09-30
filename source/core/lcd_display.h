#pragma once

#include <string>
#include <array>

namespace MS2000 {

class LCDDisplay {
private:
    static constexpr size_t LCD_WIDTH = 16;
    static constexpr size_t LCD_HEIGHT = 2;
    
    std::array<std::array<char, LCD_WIDTH>, LCD_HEIGHT> m_display;
    bool m_enabled;
    bool m_changed;
    
public:
    LCDDisplay();
    ~LCDDisplay() = default;
    
    // Display control
    void clear();
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    // Text display
    void setText(size_t line, size_t column, const std::string& text);
    void setText(size_t line, const std::string& text);
    std::string getText(size_t line) const;
    
    // Character display
    void setChar(size_t line, size_t column, char c);
    char getChar(size_t line, size_t column) const;
    
    // Display properties
    size_t getWidth() const { return LCD_WIDTH; }
    size_t getHeight() const { return LCD_HEIGHT; }
    
    // Update display (called by MCU)
    void update();
    
    // Get raw display data for GUI
    const std::array<std::array<char, LCD_WIDTH>, LCD_HEIGHT>& getDisplayData() const { return m_display; }
    
    // Check if display content changed
    bool hasChanged() const { return m_changed; }
    void clearChanged() { m_changed = false; }
};

} // namespace MS2000
