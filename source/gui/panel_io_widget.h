// fw14.txt: Panel I/O Widget for LED/Switch/VR simulation and visualization
#pragma once
#include "../core/panel_mp.h"
#include <imgui.h>
#include <string>
#include <map>

class PanelIOWidget {
public:
    PanelIOWidget() {
        // Initialize default states
        for (int i = 0; i < 16; i++) {
            m_ledStates[i] = false;
            m_ledColors[i] = {255, 255, 255}; // White by default
            m_switchStates[i] = false;
            m_vrValues[i] = 512; // Middle position
        }
    }
    
    void render(PanelMP* panelMP = nullptr) {
        ImGui::Begin("Panel I/O Simulator", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        // Wire up callbacks if PanelMP is available
        if (panelMP && !m_callbacksWired) {
            wireCallbacks(panelMP);
            m_callbacksWired = true;
        }
        
        renderLEDs();
        ImGui::Separator();
        renderSwitches();
        ImGui::Separator();
        renderVRPotentiometers();
        ImGui::Separator();
        renderTestCommands(panelMP);
        
        ImGui::End();
    }

private:
    struct LEDColor {
        uint8_t r, g, b;
    };
    
    bool m_ledStates[16];
    LEDColor m_ledColors[16];
    bool m_switchStates[16];
    uint16_t m_vrValues[16];
    bool m_callbacksWired = false;
    
    void wireCallbacks(PanelMP* panelMP) {
        // LED control callbacks
        panelMP->ledSet = [this](int ledId, bool state) {
            if (ledId >= 0 && ledId < 16) {
                m_ledStates[ledId] = state;
            }
        };
        
        panelMP->ledSetRGB = [this](int ledId, uint8_t r, uint8_t g, uint8_t b) {
            if (ledId >= 0 && ledId < 16) {
                m_ledStates[ledId] = true; // Assume setting color turns on LED
                m_ledColors[ledId] = {r, g, b};
            }
        };
        
        // Switch read callback
        panelMP->switchRead = [this](int switchId) -> bool {
            if (switchId >= 0 && switchId < 16) {
                return m_switchStates[switchId];
            }
            return false;
        };
        
        // VR read callback
        panelMP->vrRead = [this](int vrId) -> uint16_t {
            if (vrId >= 0 && vrId < 16) {
                return m_vrValues[vrId];
            }
            return 512; // Default middle position
        };
    }
    
    void renderLEDs() {
        ImGui::Text("LEDs (0-15)");
        
        // Draw LEDs in a 4x4 grid
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                int ledId = row * 4 + col;
                
                ImGui::PushID(ledId);
                
                // LED visual representation
                ImVec4 ledColor;
                if (m_ledStates[ledId]) {
                    // LED is on - use specified color
                    ledColor = ImVec4(
                        m_ledColors[ledId].r / 255.0f,
                        m_ledColors[ledId].g / 255.0f,
                        m_ledColors[ledId].b / 255.0f,
                        1.0f
                    );
                } else {
                    // LED is off - dark gray
                    ledColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                }
                
                ImGui::PushStyleColor(ImGuiCol_Button, ledColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ledColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ledColor);
                
                if (ImGui::Button(("##LED" + std::to_string(ledId)).c_str(), ImVec2(40, 40))) {
                    // Toggle LED for testing
                    m_ledStates[ledId] = !m_ledStates[ledId];
                }
                
                ImGui::PopStyleColor(3);
                
                // Label below LED
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15);
                ImGui::Text("%d", ledId);
                
                if (col < 3) ImGui::SameLine();
                
                ImGui::PopID();
            }
        }
        
        // LED controls
        ImGui::Text("LED Controls:");
        static int selectedLED = 0;
        static float ledRGB[3] = {1.0f, 1.0f, 1.0f};
        
        ImGui::SliderInt("LED ID", &selectedLED, 0, 15);
        ImGui::ColorEdit3("LED Color", ledRGB);
        
        if (ImGui::Button("Set LED ON")) {
            if (selectedLED >= 0 && selectedLED < 16) {
                m_ledStates[selectedLED] = true;
                m_ledColors[selectedLED] = {
                    (uint8_t)(ledRGB[0] * 255),
                    (uint8_t)(ledRGB[1] * 255),
                    (uint8_t)(ledRGB[2] * 255)
                };
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Set LED OFF")) {
            if (selectedLED >= 0 && selectedLED < 16) {
                m_ledStates[selectedLED] = false;
            }
        }
    }
    
    void renderSwitches() {
        ImGui::Text("Switches (0-15)");
        
        // Draw switches in a 4x4 grid
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                int switchId = row * 4 + col;
                
                ImGui::PushID(switchId + 100); // Offset to avoid LED ID conflicts
                
                bool pressed = m_switchStates[switchId];
                if (ImGui::Checkbox(("SW" + std::to_string(switchId)).c_str(), &pressed)) {
                    m_switchStates[switchId] = pressed;
                }
                
                if (col < 3) ImGui::SameLine();
                
                ImGui::PopID();
            }
        }
        
        // Switch status summary
        int pressedCount = 0;
        for (int i = 0; i < 16; i++) {
            if (m_switchStates[i]) pressedCount++;
        }
        ImGui::Text("Pressed switches: %d/16", pressedCount);
    }
    
    void renderVRPotentiometers() {
        ImGui::Text("VR Potentiometers (0-15)");
        
        // Draw VR controls in 2 columns
        for (int i = 0; i < 16; i++) {
            ImGui::PushID(i + 200); // Offset to avoid conflicts
            
            int intValue = m_vrValues[i];
            if (ImGui::SliderInt(("VR" + std::to_string(i)).c_str(), &intValue, 0, 1023)) {
                m_vrValues[i] = (uint16_t)intValue;
            }
            
            if (i % 2 == 0 && i < 15) ImGui::SameLine();
            
            ImGui::PopID();
        }
        
        // Quick preset buttons
        if (ImGui::Button("All Min")) {
            for (int i = 0; i < 16; i++) m_vrValues[i] = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("All Center")) {
            for (int i = 0; i < 16; i++) m_vrValues[i] = 512;
        }
        ImGui::SameLine();
        if (ImGui::Button("All Max")) {
            for (int i = 0; i < 16; i++) m_vrValues[i] = 1023;
        }
        ImGui::SameLine();
        if (ImGui::Button("Random")) {
            for (int i = 0; i < 16; i++) m_vrValues[i] = rand() % 1024;
        }
    }
    
    void renderTestCommands(PanelMP* panelMP) {
        ImGui::Text("Test Commands");
        
        if (!panelMP) {
            ImGui::TextDisabled("No PanelMP connection");
            return;
        }
        
        // Test frame transmission
        static int testLED = 0;
        static bool testLEDState = true;
        
        ImGui::SliderInt("Test LED", &testLED, 0, 15);
        ImGui::Checkbox("LED State", &testLEDState);
        
        if (ImGui::Button("Send LED Command")) {
            sendTestLEDCommand(panelMP, testLED, testLEDState);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Send Switch Poll")) {
            sendTestSwitchPoll(panelMP, testLED);  // Use same ID for simplicity
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Send VR Read")) {
            sendTestVRRead(panelMP, testLED);  // Use same ID for simplicity
        }
        
        // LCD test commands
        if (ImGui::Button("Send LCD Test")) {
            sendTestLCDCommands(panelMP);
        }
    }
    
    void sendTestLEDCommand(PanelMP* panelMP, int ledId, bool state) {
        // Create test frame: [HEADER] [LENGTH] [CMD_LED_SET] [LED_ID] [STATE] [CHECKSUM]
        std::vector<uint8_t> frame = {
            0xA5,           // Header
            0x03,           // Length (3 bytes payload)
            0x20,           // CMD_LED_SET
            (uint8_t)ledId, // LED ID
            state ? 0x01 : 0x00, // State
        };
        
        // Calculate checksum
        uint8_t checksum = 0;
        for (uint8_t b : frame) checksum += b;
        frame.push_back(checksum);
        
        // Send frame to PanelMP
        for (uint8_t b : frame) {
            panelMP->onSciTxByte(b, 0, true);  // Mark as test to avoid recording
        }
    }
    
    void sendTestSwitchPoll(PanelMP* panelMP, int switchId) {
        std::vector<uint8_t> frame = {
            0xA5,               // Header
            0x02,               // Length
            0x30,               // CMD_SWITCH_POLL
            (uint8_t)switchId,  // Switch ID
        };
        
        uint8_t checksum = 0;
        for (uint8_t b : frame) checksum += b;
        frame.push_back(checksum);
        
        for (uint8_t b : frame) {
            panelMP->onSciTxByte(b, 0, true);
        }
    }
    
    void sendTestVRRead(PanelMP* panelMP, int vrId) {
        std::vector<uint8_t> frame = {
            0xA5,           // Header
            0x02,           // Length
            0x31,           // CMD_VR_READ
            (uint8_t)vrId,  // VR ID
        };
        
        uint8_t checksum = 0;
        for (uint8_t b : frame) checksum += b;
        frame.push_back(checksum);
        
        for (uint8_t b : frame) {
            panelMP->onSciTxByte(b, 0, true);
        }
    }
    
    void sendTestLCDCommands(PanelMP* panelMP) {
        // Send "HELLO PANEL" message
        std::string message = "HELLO PANEL";
        
        std::vector<uint8_t> frame = {
            0xA5,                           // Header
            (uint8_t)(2 + message.length()), // Length
            0x11,                           // CMD_LCD_CLEAR
            0x12, 0x00,                     // CMD_LCD_SETPOS, position 0
        };
        
        // Add character data
        for (char c : message) {
            frame.push_back(0x13);  // CMD_LCD_DATA
            frame.push_back(c);
        }
        
        // Fix length
        frame[1] = frame.size() - 2;
        
        uint8_t checksum = 0;
        for (uint8_t b : frame) checksum += b;
        frame.push_back(checksum);
        
        for (uint8_t b : frame) {
            panelMP->onSciTxByte(b, 0, true);
        }
    }
};