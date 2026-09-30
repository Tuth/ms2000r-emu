// fw14.txt: LCD Enhancement Widget (screenshot, contrast/backlight, monospace font)
#pragma once
#include "../core/lcd_gui.h"
#include <imgui.h>
#include <string>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>

class LcdEnhancementWidget {
public:
    LcdEnhancementWidget() {
        // Initialize default settings
        m_contrast = 0.8f;
        m_backlight = 0.9f;
        m_fontScale = 1.2f;
        m_useMonospace = true;
        m_backlightColor[0] = 0.16f; // Green tint
        m_backlightColor[1] = 0.31f;
        m_backlightColor[2] = 0.16f;
    }
    
    void render(const LcdGuiSnapshot& snapshot) {
        ImGui::Begin("LCD Enhancements", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        renderScreenshotControls(snapshot);
        ImGui::Separator();
        renderDisplaySettings();
        ImGui::Separator();
        renderFontSettings();
        ImGui::Separator();
        renderEnhancedLCDPreview(snapshot);
        
        ImGui::End();
    }
    
    // Accessors for other widgets to use enhanced settings
    float getContrast() const { return m_contrast; }
    float getBacklight() const { return m_backlight; }
    float getFontScale() const { return m_fontScale; }
    bool useMonospace() const { return m_useMonospace; }
    const float* getBacklightColor() const { return m_backlightColor; }

private:
    float m_contrast = 0.8f;
    float m_backlight = 0.9f;
    float m_fontScale = 1.2f;
    bool m_useMonospace = true;
    float m_backlightColor[3] = {0.16f, 0.31f, 0.16f}; // RGB
    std::string m_lastScreenshotPath;
    
    void renderScreenshotControls(const LcdGuiSnapshot& snapshot) {
        ImGui::Text("📸 LCD Screenshot");
        
        if (ImGui::Button("Take Screenshot")) {
            takeScreenshot(snapshot);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Screenshot with Timestamp")) {
            takeTimestampedScreenshot(snapshot);
        }
        
        // Quick screenshot formats
        ImGui::Text("Quick Formats:");
        if (ImGui::Button("Debug.png")) {
            takeNamedScreenshot(snapshot, "lcd_debug.png");
        }
        ImGui::SameLine();
        if (ImGui::Button("Boot.png")) {
            takeNamedScreenshot(snapshot, "lcd_boot_sequence.png");
        }
        ImGui::SameLine();
        if (ImGui::Button("Test.png")) {
            takeNamedScreenshot(snapshot, "lcd_test_result.png");
        }
        
        // Show last screenshot path
        if (!m_lastScreenshotPath.empty()) {
            ImGui::Text("Last saved: %s", m_lastScreenshotPath.c_str());
        }
        
        // Screenshot options
        static bool includeBacklight = true;
        static bool includeCursor = true;
        static bool highRes = false;
        
        ImGui::Checkbox("Include Backlight", &includeBacklight);
        ImGui::SameLine();
        ImGui::Checkbox("Include Cursor", &includeCursor);
        ImGui::SameLine();
        ImGui::Checkbox("High Resolution", &highRes);
    }
    
    void renderDisplaySettings() {
        ImGui::Text("🎨 Display Settings");
        
        // Contrast control
        if (ImGui::SliderFloat("Contrast", &m_contrast, 0.0f, 2.0f, "%.2f")) {
            // Clamp to reasonable values
            if (m_contrast < 0.1f) m_contrast = 0.1f;
        }
        
        // Backlight intensity
        if (ImGui::SliderFloat("Backlight", &m_backlight, 0.0f, 1.0f, "%.2f")) {
            // Ensure minimum visibility
            if (m_backlight < 0.1f) m_backlight = 0.1f;
        }
        
        // Backlight color
        ImGui::ColorEdit3("Backlight Color", m_backlightColor);
        
        // Presets
        ImGui::Text("Display Presets:");
        if (ImGui::Button("Classic Green")) {
            m_contrast = 0.8f;
            m_backlight = 0.7f;
            m_backlightColor[0] = 0.0f;
            m_backlightColor[1] = 0.8f;
            m_backlightColor[2] = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Blue LCD")) {
            m_contrast = 0.9f;
            m_backlight = 0.8f;
            m_backlightColor[0] = 0.0f;
            m_backlightColor[1] = 0.4f;
            m_backlightColor[2] = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("High Contrast")) {
            m_contrast = 1.5f;
            m_backlight = 1.0f;
            m_backlightColor[0] = 1.0f;
            m_backlightColor[1] = 1.0f;
            m_backlightColor[2] = 1.0f;
        }
    }
    
    void renderFontSettings() {
        ImGui::Text("🔤 Font Settings");
        
        // Font scale
        ImGui::SliderFloat("Font Scale", &m_fontScale, 0.5f, 3.0f, "%.1f");
        
        // Monospace toggle
        ImGui::Checkbox("Use Monospace Font", &m_useMonospace);
        
        // Font presets
        ImGui::Text("Font Presets:");
        if (ImGui::Button("Small (0.8x)")) {
            m_fontScale = 0.8f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Normal (1.2x)")) {
            m_fontScale = 1.2f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Large (1.8x)")) {
            m_fontScale = 1.8f;
        }
        ImGui::SameLine();
        if (ImGui::Button("XL (2.5x)")) {
            m_fontScale = 2.5f;
        }
        
        // Font info
        if (m_useMonospace) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Using monospace font for authentic LCD look");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Using proportional font");
        }
    }
    
    void renderEnhancedLCDPreview(const LcdGuiSnapshot& snapshot) {
        ImGui::Text("🖥️ Enhanced LCD Preview");
        
        // Calculate enhanced colors based on settings
        ImVec4 enhancedBacklight = ImVec4(
            m_backlightColor[0] * m_backlight,
            m_backlightColor[1] * m_backlight, 
            m_backlightColor[2] * m_backlight,
            1.0f
        );
        
        ImVec4 enhancedText = ImVec4(
            0.9f * m_contrast,
            1.0f * m_contrast,
            0.9f * m_contrast,
            1.0f
        );
        
        // Clamp colors
        enhancedText.x = std::min(1.0f, enhancedText.x);
        enhancedText.y = std::min(1.0f, enhancedText.y);
        enhancedText.z = std::min(1.0f, enhancedText.z);
        
        // Enhanced LCD frame
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        
        float charWidth = 16.0f * m_fontScale;
        float charHeight = 22.0f * m_fontScale;
        float padding = 12.0f * m_fontScale;
        
        ImVec2 size(16 * charWidth + 2 * padding, 2 * charHeight + 2 * padding);
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        
        // Enhanced background with backlight
        dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(enhancedBacklight), 8.0f);
        dl->AddRect(p0, p1, IM_COL32(100, 100, 100, 255), 8.0f, 0, 2.0f);
        
        ImGui::Dummy(size);
        
        if (!snapshot.displayOn) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + size.x/2 - 50, p0.y + size.y/2 - 10));
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "DISPLAY OFF");
            return;
        }
        
        // Enhanced text rendering
        ImGui::PushStyleColor(ImGuiCol_Text, enhancedText);
        ImGui::SetWindowFontScale(m_fontScale);
        
        // Line 0
        ImGui::SetCursorScreenPos(ImVec2(p0.x + padding, p0.y + padding));
        renderEnhancedText(snapshot.line0, 16);
        
        // Line 1  
        ImGui::SetCursorScreenPos(ImVec2(p0.x + padding, p0.y + padding + charHeight));
        renderEnhancedText(snapshot.line1, 16);
        
        // Enhanced cursor rendering
        if (snapshot.cursorOn) {
            static float blinkTimer = 0.0f;
            blinkTimer += ImGui::GetIO().DeltaTime;
            
            bool showCursor = !snapshot.blinkOn || (fmod(blinkTimer, 1.0f) < 0.5f);
            
            if (showCursor && snapshot.curLine >= 0 && snapshot.curCol >= 0 && snapshot.curCol < 16) {
                float cursorX = p0.x + padding + snapshot.curCol * charWidth;
                float cursorY = p0.y + padding + (snapshot.curLine * charHeight) + charHeight - 4;
                
                ImVec4 cursorColor = ImVec4(1.0f, 1.0f, 0.0f, 0.8f); // Yellow cursor
                dl->AddRectFilled(
                    ImVec2(cursorX, cursorY),
                    ImVec2(cursorX + charWidth - 2, cursorY + 3),
                    ImGui::ColorConvertFloat4ToU32(cursorColor)
                );
            }
        }
        
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f); // Reset font scale
    }
    
    void renderEnhancedText(const char* text, int maxChars) {
        for (int i = 0; i < maxChars; i++) {
            char ch = text[i];
            if (ch == '\0') break;
            
            if (ch >= 0 && ch < 8) {
                // Custom CGRAM character - render as placeholder for now
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚬");
            } else {
                // Regular character
                char single[2] = {ch, '\0'};
                ImGui::TextUnformatted(single);
            }
            
            if (i < maxChars - 1) ImGui::SameLine();
        }
    }
    
    void takeScreenshot(const LcdGuiSnapshot& snapshot) {
        std::string filename = "lcd_screenshot.png";
        saveScreenshotData(snapshot, filename);
        m_lastScreenshotPath = filename;
    }
    
    void takeTimestampedScreenshot(const LcdGuiSnapshot& snapshot) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::ostringstream oss;
        oss << "lcd_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".png";
        std::string filename = oss.str();
        
        saveScreenshotData(snapshot, filename);
        m_lastScreenshotPath = filename;
    }
    
    void takeNamedScreenshot(const LcdGuiSnapshot& snapshot, const std::string& filename) {
        saveScreenshotData(snapshot, filename);
        m_lastScreenshotPath = filename;
    }
    
    void saveScreenshotData(const LcdGuiSnapshot& snapshot, const std::string& filename) {
        // For now, save as text representation (could be enhanced to actual PNG)
        std::ofstream file(filename + ".txt"); // Add .txt for text format
        
        if (!file.is_open()) {
            m_lastScreenshotPath = "ERROR: Could not save " + filename;
            return;
        }
        
        file << "MS2000 LCD Screenshot\n";
        file << "=====================\n";
        file << "Timestamp: " << getCurrentTimeString() << "\n";
        file << "Display ON: " << (snapshot.displayOn ? "YES" : "NO") << "\n";
        file << "Cursor ON: " << (snapshot.cursorOn ? "YES" : "NO") << "\n";
        file << "Blink ON: " << (snapshot.blinkOn ? "YES" : "NO") << "\n";
        file << "Cursor Position: Line " << snapshot.curLine << ", Col " << snapshot.curCol << "\n";
        file << "\nLCD Content:\n";
        file << "+------------------+\n";
        file << "|" << std::string(snapshot.line0, 16) << "|\n";
        file << "|" << std::string(snapshot.line1, 16) << "|\n";  
        file << "+------------------+\n";
        file << "\nSettings:\n";
        file << "Contrast: " << m_contrast << "\n";
        file << "Backlight: " << m_backlight << "\n";
        file << "Font Scale: " << m_fontScale << "\n";
        file << "Monospace: " << (m_useMonospace ? "YES" : "NO") << "\n";
        
        file.close();
        
        // Note: Real PNG implementation would require image library (stb_image_write, etc.)
        // For now, we save as structured text which is useful for debugging
    }
    
    std::string getCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
};