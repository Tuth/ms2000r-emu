#include "lcd_screenshot.h"
#include <ctime>
#include <cstdio>
#include <filesystem>

// stb_image_write for PNG generation
#define STB_IMAGE_WRITE_SIMPLE_IMPLEMENTATION
#include "stb_image_write_simple.h"

// Global instance
LcdScreenshot g_lcdScreenshot;

// Simplified 5x7 font data for HD44780 characters (key characters only)
uint8_t LcdFont::CHAR_DATA[256][8];

// Initialize font data at runtime
static void initFontData() {
    static bool initialized = false;
    if (initialized) return;
    
    // Attempt to load standard Hitachi HD44780 A00 CGROM binary (4096 bytes)
    const char* paths[] = {
        "hd44780_a00.bin",
        "C:\\workspace\\MS2000\\hd44780_a00.bin",
        "..\\hd44780_a00.bin",
        "..\\..\\hd44780_a00.bin"
    };
    
    FILE* f = nullptr;
    for (const char* path : paths) {
        f = fopen(path, "rb");
        if (f) break;
    }
    
    if (f) {
        uint8_t buffer[4096];
        size_t bytesRead = fread(buffer, 1, 4096, f);
        fclose(f);
        if (bytesRead == 4096) {
            for (int i = 0; i < 256; i++) {
                for (int j = 0; j < 8; j++) {
                    // Each character block is 16 bytes. First 8 bytes are the 5x8 character dots.
                    LcdFont::CHAR_DATA[i][j] = buffer[i * 16 + j];
                }
            }
            initialized = true;
            return;
        }
    }
    
    // Fallback: Initialize all to space
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 8; j++) {
            LcdFont::CHAR_DATA[i][j] = 0x00;
        }
    }
    
    // Key characters only (Fallback subset)
    uint8_t space[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t A[8] = {0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00};
    uint8_t B[8] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00};
    uint8_t C[8] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00};
    uint8_t D[8] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C, 0x00};
    uint8_t E[8] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00};
    uint8_t F[8] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00};
    uint8_t G[8] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00};
    uint8_t H[8] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00};
    uint8_t I[8] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00};
    uint8_t J[8] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00};
    uint8_t K[8] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00};
    uint8_t L[8] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00};
    uint8_t M[8] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00};
    uint8_t N[8] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00};
    uint8_t O[8] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00};
    uint8_t P[8] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00};
    uint8_t Q[8] = {0x0E, 0x11, 0x11, 0x15, 0x12, 0x11, 0x0E, 0x01};
    uint8_t R[8] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00};
    uint8_t S[8] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00};
    uint8_t T[8] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00};
    uint8_t U[8] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00};
    uint8_t V[8] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00};
    uint8_t W[8] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x00};
    uint8_t X[8] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00};
    uint8_t Y[8] = {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x00};
    uint8_t Z[8] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00};
    
    // Digits
    uint8_t d0[8] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00};
    uint8_t d1[8] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00};
    uint8_t d2[8] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00};
    uint8_t d3[8] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00};
    uint8_t d4[8] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00};
    uint8_t d5[8] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00};
    uint8_t d6[8] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00};
    uint8_t d7[8] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00};
    uint8_t d8[8] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00};
    uint8_t d9[8] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00};

    // Copy patterns
    memcpy(LcdFont::CHAR_DATA[' '], space, 8);
    memcpy(LcdFont::CHAR_DATA['A'], A, 8);
    memcpy(LcdFont::CHAR_DATA['B'], B, 8);
    memcpy(LcdFont::CHAR_DATA['C'], C, 8);
    memcpy(LcdFont::CHAR_DATA['D'], D, 8);
    memcpy(LcdFont::CHAR_DATA['E'], E, 8);
    memcpy(LcdFont::CHAR_DATA['F'], F, 8);
    memcpy(LcdFont::CHAR_DATA['G'], G, 8);
    memcpy(LcdFont::CHAR_DATA['H'], H, 8);
    memcpy(LcdFont::CHAR_DATA['I'], I, 8);
    memcpy(LcdFont::CHAR_DATA['J'], J, 8);
    memcpy(LcdFont::CHAR_DATA['K'], K, 8);
    memcpy(LcdFont::CHAR_DATA['L'], L, 8);
    memcpy(LcdFont::CHAR_DATA['M'], M, 8);
    memcpy(LcdFont::CHAR_DATA['N'], N, 8);
    memcpy(LcdFont::CHAR_DATA['O'], O, 8);
    memcpy(LcdFont::CHAR_DATA['P'], P, 8);
    memcpy(LcdFont::CHAR_DATA['Q'], Q, 8);
    memcpy(LcdFont::CHAR_DATA['R'], R, 8);
    memcpy(LcdFont::CHAR_DATA['S'], S, 8);
    memcpy(LcdFont::CHAR_DATA['T'], T, 8);
    memcpy(LcdFont::CHAR_DATA['U'], U, 8);
    memcpy(LcdFont::CHAR_DATA['V'], V, 8);
    memcpy(LcdFont::CHAR_DATA['W'], W, 8);
    memcpy(LcdFont::CHAR_DATA['X'], X, 8);
    memcpy(LcdFont::CHAR_DATA['Y'], Y, 8);
    memcpy(LcdFont::CHAR_DATA['Z'], Z, 8);
    
    memcpy(LcdFont::CHAR_DATA['0'], d0, 8);
    memcpy(LcdFont::CHAR_DATA['1'], d1, 8);
    memcpy(LcdFont::CHAR_DATA['2'], d2, 8);
    memcpy(LcdFont::CHAR_DATA['3'], d3, 8);
    memcpy(LcdFont::CHAR_DATA['4'], d4, 8);
    memcpy(LcdFont::CHAR_DATA['5'], d5, 8);
    memcpy(LcdFont::CHAR_DATA['6'], d6, 8);
    memcpy(LcdFont::CHAR_DATA['7'], d7, 8);
    memcpy(LcdFont::CHAR_DATA['8'], d8, 8);
    memcpy(LcdFont::CHAR_DATA['9'], d9, 8);
    
    initialized = true;
}

std::string LcdScreenshot::makeTimestampedPath(const char* prefix, const char* ext) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    
    char filename[256];
    std::snprintf(filename, sizeof(filename), "%s\\%s_%04d%02d%02d_%02d%02d%02d_f%04u.%s",
        screenshotDir_.c_str(), prefix, 
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        frameCounter_++, ext);
    return filename;
}

bool LcdScreenshot::writePNG(const std::string& filepath, const LcdFramebuffer& fb) {
    // Use simple PNG writer for grayscale image
    int result = stbi_write_png_simple(filepath.c_str(), 
        LcdFramebuffer::WIDTH, LcdFramebuffer::HEIGHT, 
        1, // 1 channel (grayscale)
        fb.pixels, 
        LcdFramebuffer::WIDTH); // stride
    return result != 0;
}

bool LcdScreenshot::captureScreenshot(const char lcd[2][17]) {
    // Initialize font data
    initFontData();
    
    // Create screenshots directory if it doesn't exist
    std::filesystem::create_directories(screenshotDir_);
    
    // Convert LCD character array to framebuffer
    LcdFramebuffer fb;
    fb.clear();
    
    // Render each character
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 16; col++) {
            char ch = lcd[row][col];
            if (ch == 0) ch = ' '; // Handle null terminators
            LcdFont::renderChar(fb, ch, col, row);
        }
    }
    
    // Generate timestamped filename and save
    std::string filepath = makeTimestampedPath("lcd", "png");
    return writePNG(filepath, fb);
}

bool LcdScreenshot::saveFramebuffer(const LcdFramebuffer& fb, const std::string& filename) {
    std::filesystem::create_directories(screenshotDir_);
    std::string filepath = screenshotDir_ + "\\" + filename;
    return writePNG(filepath, fb);
}