#pragma once
#include <cstdint>
#include <string>
#include <vector>

// fw22.txt: Timestamped PNG LCD screenshot functionality
// stb_image_write integration for PNG generation

// LCD Framebuffer structure (16x2 character LCD as 128x64 bitmap)
struct LcdFramebuffer {
    static const int WIDTH = 128;   // 16 chars * 8 pixels/char
    static const int HEIGHT = 64;   // 2 rows * 32 pixels/row (with spacing)
    uint8_t pixels[WIDTH * HEIGHT]; // Grayscale: 0=black, 255=white
    
    void clear() {
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            pixels[i] = 240; // Light gray background
        }
    }
};

// 5x7 font for HD44780 character rendering
class LcdFont {
public:
    static uint8_t CHAR_DATA[256][8]; // 5x7 chars in 8-byte format
    
    // Render single character at position (charX, charY) in framebuffer
    static void renderChar(LcdFramebuffer& fb, char ch, int charX, int charY) {
        if (charX < 0 || charX >= 16 || charY < 0 || charY >= 2) return;
        
        uint8_t charIndex = static_cast<uint8_t>(ch);
        const uint8_t* charData = CHAR_DATA[charIndex];
        
        int pixelX = charX * 8;
        int pixelY = charY * 32; // Row spacing for visibility
        
        for (int row = 0; row < 8; row++) {
            uint8_t rowData = charData[row];
            for (int col = 0; col < 5; col++) {
                if (rowData & (0x10 >> col)) {
                    int px = pixelX + col;
                    int py = pixelY + row;
                    if (px < LcdFramebuffer::WIDTH && py < LcdFramebuffer::HEIGHT) {
                        fb.pixels[py * LcdFramebuffer::WIDTH + px] = 0; // Black pixel
                    }
                }
            }
        }
    }
};

// LCD Screenshot Manager
class LcdScreenshot {
private:
    std::string screenshotDir_;
    uint32_t frameCounter_;

public:
    explicit LcdScreenshot(const std::string& dir = "screenshots") 
        : screenshotDir_(dir), frameCounter_(0) {}
    
    // Create timestamped screenshot from LCD state
    bool captureScreenshot(const char lcd[2][17]);
    
    // Create screenshot from raw framebuffer  
    bool saveFramebuffer(const LcdFramebuffer& fb, const std::string& filename);
    
private:
    std::string makeTimestampedPath(const char* prefix, const char* ext);
    bool writePNG(const std::string& filepath, const LcdFramebuffer& fb);
};

// Global screenshot manager instance
extern LcdScreenshot g_lcdScreenshot;