#include "lcd_display.h"
#include <cstring>
#include <iostream>

namespace MS2000 {

LCDDisplay::LCDDisplay() : m_enabled(true), m_changed(false) {
    clear();
}

void LCDDisplay::clear() {
    for (auto& line : m_display) {
        line.fill(' ');
    }
    m_changed = true;
}

void LCDDisplay::setText(size_t line, size_t column, const std::string& text) {
    if (line >= LCD_HEIGHT || column >= LCD_WIDTH) {
        return;
    }
    
    size_t remaining = LCD_WIDTH - column;
    size_t textLength = std::min(text.length(), remaining);
    
    for (size_t i = 0; i < textLength; ++i) {
        m_display[line][column + i] = text[i];
    }
    m_changed = true;
}

void LCDDisplay::setText(size_t line, const std::string& text) {
    setText(line, 0, text);
}

std::string LCDDisplay::getText(size_t line) const {
    if (line >= LCD_HEIGHT) {
        return "";
    }
    
    std::string result;
    for (char c : m_display[line]) {
        result += c;
    }
    
    // Trim trailing spaces
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    
    return result;
}

void LCDDisplay::setChar(size_t line, size_t column, char c) {
    if (line < LCD_HEIGHT && column < LCD_WIDTH) {
        m_display[line][column] = c;
        m_changed = true;
    }
}

char LCDDisplay::getChar(size_t line, size_t column) const {
    if (line < LCD_HEIGHT && column < LCD_WIDTH) {
        return m_display[line][column];
    }
    return ' ';
}

void LCDDisplay::update() {
    if (!m_enabled) {
        return;
    }
    
    // For now, just print to console
    // In a real implementation, this would update the GUI
    std::cout << "LCD Display:" << std::endl;
    for (size_t i = 0; i < LCD_HEIGHT; ++i) {
        std::cout << "Line " << i << ": [" << getText(i) << "]" << std::endl;
    }
}

} // namespace MS2000
