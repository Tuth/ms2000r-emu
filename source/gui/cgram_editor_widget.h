// fw14.txt: CGRAM Editor Widget for custom character patterns
#pragma once
#include "../core/lcd_gui.h"
#include <imgui.h>
#include <string>

class CgramEditorWidget {
public:
    void render(LcdGuiBuffer* lcdBuffer = nullptr) {
        ImGui::Begin("CGRAM Character Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        // Get current LCD snapshot if buffer available
        LcdGuiSnapshot snapshot{};
        if (lcdBuffer) {
            snapshot = lcdBuffer->snapshot();
        }
        
        renderCharacterGrid(snapshot);
        ImGui::Separator();
        renderCharacterEditor();
        ImGui::Separator();
        renderTestArea(lcdBuffer);
        
        ImGui::End();
    }

private:
    int m_selectedChar = 0;  // Currently selected character (0-7)
    uint8_t m_editPattern[8][8]{};  // Pattern being edited
    bool m_patternLoaded = false;
    
    void renderCharacterGrid(const LcdGuiSnapshot& snapshot) {
        ImGui::Text("Custom Characters (0-7):");
        
        // Display all 8 custom characters in a 4x2 grid
        for (int ch = 0; ch < 8; ch++) {
            ImGui::PushID(ch);
            
            if (ImGui::Button(("Char " + std::to_string(ch)).c_str(), ImVec2(80, 80))) {
                m_selectedChar = ch;
                loadPatternFromSnapshot(ch, snapshot);
            }
            
            // Render character preview in button
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 buttonMin = ImGui::GetItemRectMin();
            ImVec2 buttonMax = ImGui::GetItemRectMax();
            
            // Draw character pattern
            renderCharacterPreview(drawList, buttonMin + ImVec2(20, 10), ch, snapshot, 
                                 ch == m_selectedChar ? IM_COL32(255, 255, 0, 255) : IM_COL32(200, 200, 200, 255));
            
            ImGui::PopID();
            
            // 4 characters per row
            if ((ch % 4) != 3) ImGui::SameLine();
        }
        
        ImGui::Text("Selected: Character %d", m_selectedChar);
    }
    
    void renderCharacterEditor() {
        ImGui::Text("Edit Character %d Pattern (5x8 pixels):", m_selectedChar);
        
        // 8x5 pixel grid editor
        for (int row = 0; row < 8; row++) {
            ImGui::PushID(row);
            for (int col = 0; col < 5; col++) {
                ImGui::PushID(col);
                
                bool pixel = (m_editPattern[m_selectedChar][row] & (1 << (4 - col))) != 0;
                if (ImGui::Checkbox("", &pixel)) {
                    if (pixel) {
                        m_editPattern[m_selectedChar][row] |= (1 << (4 - col));
                    } else {
                        m_editPattern[m_selectedChar][row] &= ~(1 << (4 - col));
                    }
                }
                
                if (col < 4) ImGui::SameLine();
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        
        // Pattern display as hex
        ImGui::Text("Hex Pattern:");
        for (int row = 0; row < 8; row++) {
            ImGui::Text("Row %d: 0x%02X", row, m_editPattern[m_selectedChar][row] & 0x1F);
            if (row < 7) ImGui::SameLine();
        }
        
        // Control buttons
        if (ImGui::Button("Clear Pattern")) {
            for (int row = 0; row < 8; row++) {
                m_editPattern[m_selectedChar][row] = 0;
            }
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Fill Pattern")) {
            for (int row = 0; row < 8; row++) {
                m_editPattern[m_selectedChar][row] = 0x1F;  // All 5 bits set
            }
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Load Preset")) {
            loadPresetPattern(m_selectedChar);
        }
    }
    
    void renderTestArea(LcdGuiBuffer* lcdBuffer) {
        ImGui::Text("Test Area:");
        
        if (!lcdBuffer) {
            ImGui::TextDisabled("No LCD buffer available");
            return;
        }
        
        static char testText[32] = "Hello \x00\x01\x02\x03!";
        ImGui::InputText("Test Text", testText, sizeof(testText));
        ImGui::Text("Use \\x00-\\x07 for custom characters");
        
        if (ImGui::Button("Send to LCD")) {
            // Clear LCD and write test pattern
            lcdBuffer->onCmd(0x01);  // Clear display
            lcdBuffer->onCmd(0x80);  // Set DDRAM address to 0
            
            // Send the test text
            for (int i = 0; testText[i] != '\0' && i < 16; i++) {
                lcdBuffer->onData((uint8_t)testText[i]);
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Upload Pattern")) {
            uploadPatternToLcd(lcdBuffer);
        }
    }
    
    void renderCharacterPreview(ImDrawList* drawList, const ImVec2& pos, int charIndex, 
                              const LcdGuiSnapshot& snapshot, unsigned int color) {
        const float pixelSize = 4.0f;
        const float charWidth = 5 * pixelSize;
        const float charHeight = 8 * pixelSize;
        
        for (int row = 0; row < 8; row++) {
            uint8_t pattern;
            if (m_patternLoaded && charIndex == m_selectedChar) {
                pattern = m_editPattern[charIndex][row] & 0x1F;
            } else {
                pattern = snapshot.cgram[charIndex][row] & 0x1F;
            }
            
            for (int col = 0; col < 5; col++) {
                if (pattern & (1 << (4 - col))) {
                    float px = pos.x + col * pixelSize;
                    float py = pos.y + row * pixelSize;
                    drawList->AddRectFilled(ImVec2(px, py), ImVec2(px + pixelSize, py + pixelSize), color);
                }
            }
        }
    }
    
    void loadPatternFromSnapshot(int charIndex, const LcdGuiSnapshot& snapshot) {
        for (int row = 0; row < 8; row++) {
            m_editPattern[charIndex][row] = snapshot.cgram[charIndex][row];
        }
        m_patternLoaded = true;
    }
    
    void loadPresetPattern(int charIndex) {
        // Load some preset patterns for testing
        const uint8_t presets[8][8] = {
            // Character 0: Heart
            {0x00, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00},
            // Character 1: Arrow Up
            {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x00},
            // Character 2: Arrow Down  
            {0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04, 0x00},
            // Character 3: Music Note
            {0x02, 0x03, 0x02, 0x0E, 0x1E, 0x0C, 0x00, 0x00},
            // Character 4: Block
            {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F},
            // Character 5: Progress Bar
            {0x10, 0x18, 0x1C, 0x1E, 0x1C, 0x18, 0x10, 0x00},
            // Character 6: Degree Symbol
            {0x06, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00, 0x00},
            // Character 7: Custom Pattern
            {0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A}
        };
        
        for (int row = 0; row < 8; row++) {
            m_editPattern[charIndex][row] = presets[charIndex][row];
        }
        m_patternLoaded = true;
    }
    
    void uploadPatternToLcd(LcdGuiBuffer* lcdBuffer) {
        if (!lcdBuffer) return;
        
        // Set CGRAM address for selected character
        uint8_t cgramBase = (m_selectedChar << 3);  // Each character takes 8 bytes
        lcdBuffer->onCmd(0x40 | cgramBase);
        
        // Upload the 8 rows of pattern data
        for (int row = 0; row < 8; row++) {
            lcdBuffer->onData(m_editPattern[m_selectedChar][row] & 0x1F);
        }
        
        // Return to DDRAM mode
        lcdBuffer->onCmd(0x80);
    }
};