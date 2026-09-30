#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <conio.h>
#include "core/panel_if_adapter.h"
#include "core/h8s2350_v2_1.h"
#include "core/lcd_display.h"

using namespace MS2000;

class MS2000GUI {
private:
    TestPanelIO m_panel;
    PanelIFAdapter m_panelAdapter;
    H8SBus m_bus;
    Peripherals m_periph;
    MS2000::LCDDisplay m_lcd;
    
    bool m_running;
    int m_selectedRow;
    int m_selectedCol;
    int m_selectedPot;
    
public:
    MS2000GUI() 
        : m_panelAdapter(m_panel)
        , m_periph(m_bus, m_panelAdapter)
        , m_running(true)
        , m_selectedRow(0)
        , m_selectedCol(0)
        , m_selectedPot(0)
    {
        // Initialize some test data
        m_panel.setButton(0, 0, true);
        m_panel.setButton(2, 3, true);
        m_panel.setPotentiometer(0, 100);
        m_panel.setPotentiometer(1, 300);
        m_panel.setPotentiometer(2, 500);
        m_panel.setPotentiometer(3, 700);
        
        // Initialize LCD with test data
        m_lcd.setText(0, "MS2000 READY");
        m_lcd.setText(1, "Bank: COMMON  Prog: 001");
    }
    
    void run() {
        while (m_running) {
            clearScreen();
            drawMainInterface();
            handleInput();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void clearScreen() {
        #ifdef _WIN32
        system("cls");
        #else
        system("clear");
        #endif
    }
    
    void drawMainInterface() {
        std::cout << "=== KORG MS-2000 Panel Emulator ===" << std::endl;
        std::cout << "Firmware v2.1 - PanelIF Adapter Active" << std::endl;
        std::cout << std::endl;
        
        drawLCD();
        std::cout << std::endl;
        drawButtonMatrix();
        std::cout << std::endl;
        drawLEDMatrix();
        std::cout << std::endl;
        drawPotentiometers();
        std::cout << std::endl;
        drawStatus();
        std::cout << std::endl;
        drawControls();
    }
    
    void drawButtonMatrix() {
        std::cout << "Button Matrix (8x8):" << std::endl;
        std::cout << "   ";
        for (int col = 0; col < 8; col++) {
            std::cout << std::setw(2) << col << " ";
        }
        std::cout << std::endl;
        
        for (int row = 0; row < 8; row++) {
            std::cout << std::setw(2) << row << " ";
            for (int col = 0; col < 8; col++) {
                bool pressed = !m_panel.getLED(row, col); // Inverted for buttons
                char symbol = pressed ? 'X' : '.';
                if (row == m_selectedRow && col == m_selectedCol) {
                    std::cout << "[" << symbol << "]";
                } else {
                    std::cout << " " << symbol << " ";
                }
            }
            std::cout << std::endl;
        }
    }
    
    void drawLEDMatrix() {
        std::cout << "LED Matrix (8x8):" << std::endl;
        std::cout << "   ";
        for (int col = 0; col < 8; col++) {
            std::cout << std::setw(2) << col << " ";
        }
        std::cout << std::endl;
        
        for (int row = 0; row < 8; row++) {
            std::cout << std::setw(2) << row << " ";
            for (int col = 0; col < 8; col++) {
                bool lit = m_panel.getLED(row, col);
                char symbol = lit ? 'O' : '.';
                std::cout << " " << symbol << " ";
            }
            std::cout << std::endl;
        }
    }
    
    void drawPotentiometers() {
        std::cout << "Potentiometers (8 channels):" << std::endl;
        for (int i = 0; i < 8; i++) {
            uint16_t value = 0;
            // Read from panel
            m_panel.setADCMuxChannel(i);
            value = m_panel.readADC();
            
            std::cout << "Pot " << i << ": [";
            int barLength = (value * 20) / 1023; // Scale to 20 chars
            for (int j = 0; j < 20; j++) {
                if (j < barLength) {
                    std::cout << "#";
                } else {
                    std::cout << "-";
                }
            }
            std::cout << "] " << std::setw(4) << value;
            
            if (i == m_selectedPot) {
                std::cout << " <--";
            }
            std::cout << std::endl;
        }
    }
    
    void drawStatus() {
        std::cout << "Status:" << std::endl;
        std::cout << "  Last Row: 0x" << std::hex << (int)m_periph.last_row << std::dec << std::endl;
        std::cout << "  Last LED Row: 0x" << std::hex << (int)m_periph.last_led_row << std::dec << std::endl;
        std::cout << "  Last LED Cols: 0x" << std::hex << (int)m_periph.last_led_cols << std::dec << std::endl;
        
        // Show firmware address access
        std::cout << "  Firmware Addresses:" << std::endl;
        std::cout << "    PORT COL (0x" << std::hex << MS2K_PORT_COL_ADDR << "): 0x" 
                  << std::setw(2) << std::setfill('0') << (int)m_bus.read8(MS2K_PORT_COL_ADDR) << std::dec << std::endl;
        std::cout << "    PORT ROW (0x" << std::hex << MS2K_PORT_ROW_ADDR << "): 0x" 
                  << std::setw(2) << std::setfill('0') << (int)m_bus.read8(MS2K_PORT_ROW_ADDR) << std::dec << std::endl;
    }
    
    void drawLCD() {
        std::cout << "LCD Display (16x2):" << std::endl;
        std::cout << "+--------------------------------+" << std::endl;
        std::cout << "|";
        
        // Get LCD text from the actual LCD object
        std::string line1 = m_lcd.getText(0);
        std::string line2 = m_lcd.getText(1);
        
        // Display first line (16 characters)
        for (int i = 0; i < 16; i++) {
            if (i < line1.length()) {
                std::cout << line1[i];
            } else {
                std::cout << " ";
            }
        }
        std::cout << "|" << std::endl;
        
        std::cout << "|";
        // Display second line (16 characters)
        for (int i = 0; i < 16; i++) {
            if (i < line2.length()) {
                std::cout << line2[i];
            } else {
                std::cout << " ";
            }
        }
        std::cout << "|" << std::endl;
        std::cout << "+--------------------------------+" << std::endl;
    }
    
    void drawControls() {
        std::cout << "Controls:" << std::endl;
        std::cout << "  Arrow Keys: Navigate button/LED matrix" << std::endl;
        std::cout << "  Space: Toggle button/LED" << std::endl;
        std::cout << "  +/-: Adjust potentiometer" << std::endl;
        std::cout << "  Tab: Switch between button/LED/pot selection" << std::endl;
        std::cout << "  D: Test LCD display" << std::endl;
        std::cout << "  Q: Quit" << std::endl;
        std::cout << std::endl;
        std::cout << "Selected: ";
        if (m_selectedPot >= 0) {
            std::cout << "Potentiometer " << m_selectedPot;
        } else {
            std::cout << "Button/LED [" << m_selectedRow << "," << m_selectedCol << "]";
        }
    }
    
    void handleInput() {
        if (_kbhit()) {
            int key = _getch();
            
            if (key == 224) { // Arrow key prefix
                key = _getch();
                handleArrowKey(key);
            } else {
                handleRegularKey(key);
            }
        }
    }
    
    void handleArrowKey(int key) {
        switch (key) {
            case 72: // Up
                if (m_selectedPot >= 0) {
                    m_selectedPot = (m_selectedPot - 1 + 8) % 8;
                } else {
                    m_selectedRow = (m_selectedRow - 1 + 8) % 8;
                }
                break;
            case 80: // Down
                if (m_selectedPot >= 0) {
                    m_selectedPot = (m_selectedPot + 1) % 8;
                } else {
                    m_selectedRow = (m_selectedRow + 1) % 8;
                }
                break;
            case 75: // Left
                if (m_selectedPot >= 0) {
                    // Decrease potentiometer value
                    uint16_t current = 0;
                    m_panel.setADCMuxChannel(m_selectedPot);
                    current = m_panel.readADC();
                    if (current > 0) {
                        m_panel.setPotentiometer(m_selectedPot, current - 50);
                    }
                } else {
                    m_selectedCol = (m_selectedCol - 1 + 8) % 8;
                }
                break;
            case 77: // Right
                if (m_selectedPot >= 0) {
                    // Increase potentiometer value
                    uint16_t current = 0;
                    m_panel.setADCMuxChannel(m_selectedPot);
                    current = m_panel.readADC();
                    if (current < 1023) {
                        m_panel.setPotentiometer(m_selectedPot, current + 50);
                    }
                } else {
                    m_selectedCol = (m_selectedCol + 1) % 8;
                }
                break;
        }
    }
    
    void handleRegularKey(int key) {
        switch (key) {
            case ' ':
            case 13: // Enter
                if (m_selectedPot >= 0) {
                    // Toggle potentiometer between min/max
                    uint16_t current = 0;
                    m_panel.setADCMuxChannel(m_selectedPot);
                    current = m_panel.readADC();
                    if (current > 512) {
                        m_panel.setPotentiometer(m_selectedPot, 0);
                    } else {
                        m_panel.setPotentiometer(m_selectedPot, 1023);
                    }
                } else {
                    // Toggle button/LED
                    bool current = m_panel.getLED(m_selectedRow, m_selectedCol);
                    m_panel.setButton(m_selectedRow, m_selectedCol, !current);
                }
                break;
            case 9: // Tab
                if (m_selectedPot >= 0) {
                    m_selectedPot = -1; // Switch to button/LED mode
                } else {
                    m_selectedPot = 0; // Switch to potentiometer mode
                }
                break;
            case 'q':
            case 'Q':
                m_running = false;
                break;
            case '+':
            case '=':
                if (m_selectedPot >= 0) {
                    uint16_t current = 0;
                    m_panel.setADCMuxChannel(m_selectedPot);
                    current = m_panel.readADC();
                    if (current < 1023) {
                        m_panel.setPotentiometer(m_selectedPot, current + 100);
                    }
                }
                break;
            case '-':
                if (m_selectedPot >= 0) {
                    uint16_t current = 0;
                    m_panel.setADCMuxChannel(m_selectedPot);
                    current = m_panel.readADC();
                    if (current > 0) {
                        m_panel.setPotentiometer(m_selectedPot, current - 100);
                    }
                }
                break;
            case 'd':
            case 'D':
                // Test LCD display
                static int lcdTestCounter = 0;
                lcdTestCounter++;
                m_lcd.setText(0, "LCD TEST " + std::to_string(lcdTestCounter));
                m_lcd.setText(1, "Time: " + std::to_string(lcdTestCounter) + "s");
                break;
        }
    }
};

int main() {
    std::cout << "Starting MS-2000 Panel GUI..." << std::endl;
    std::cout << "Press any key to continue..." << std::endl;
    _getch();
    
    MS2000GUI gui;
    gui.run();
    
    std::cout << "GUI closed." << std::endl;
    return 0;
}
