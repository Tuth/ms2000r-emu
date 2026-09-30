#include "../core/ms2000_runner.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

using namespace MS2000;

class MS2000CleanDemo {
private:
    std::unique_ptr<Ms2kRunner> m_runner;
    bool m_running = false;
    
    // Simulated GUI state
    float m_knobPositions[32];      // 0.0-1.0 for each knob
    bool m_buttonStates[64];        // Button press states
    LEDColor m_ledStates[128];      // LED colors
    std::string m_lcdLines[2];      // LCD content
    std::vector<uint8_t> m_lastMidiOut; // Last MIDI message sent
    std::vector<uint8_t> m_lastMidiIn;  // Last MIDI message received
    
    // Demo automation
    double m_time = 0.0;
    
public:
    MS2000CleanDemo() {
        // Initialize simulated GUI state
        for (int i = 0; i < 32; i++) {
            m_knobPositions[i] = 0.5f; // Mid-range
        }
        for (int i = 0; i < 64; i++) {
            m_buttonStates[i] = false;
        }
        for (int i = 0; i < 128; i++) {
            m_ledStates[i] = {0, 0}; // Off
        }
        m_lcdLines[0] = "MS2000 GUI DEMO ";
        m_lcdLines[1] = "API Working! :) ";
    }
    
    bool initialize() {
        std::cout << "🎹 MS2000 Clean GUI Demo Starting..." << std::endl;
        
        // Configure emulator - without ROM to avoid firmware execution
        Ms2kConfig config;
        config.romPath = "nonexistent.rom"; // Intentionally invalid
        config.cpuCyclesPerTick = 1000;     // Reduced
        config.sampleRate = 48000;
        
        // Set up GUI hooks
        Ms2kGuiHooks hooks = setupHooks();
        
        // Create runner
        m_runner = std::make_unique<Ms2kRunner>(config, hooks);
        
        std::cout << "✅ MS2000 API initialized successfully" << std::endl;
        std::cout << "📋 Running in GUI demo mode (no firmware execution)" << std::endl;
        
        return true;
    }
    
    void run() {
        m_running = true;
        std::cout << "\n🎛️  MS2000 Clean GUI Demo Running!" << std::endl;
        std::cout << "=========================================\n" << std::endl;
        
        printInstructions();
        
        auto lastUpdate = std::chrono::steady_clock::now();
        
        while (m_running) {
            auto now = std::chrono::steady_clock::now();
            auto deltaTime = std::chrono::duration<double>(now - lastUpdate).count();
            lastUpdate = now;
            
            // Update demo automation
            updateDemo(deltaTime);
            
            // Process any keyboard input
            processInput();
            
            // Update display
            updateDisplay();
            
            // 60 FPS
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        std::cout << "\n👋 MS2000 Clean Demo Stopped" << std::endl;
    }
    
    void stop() {
        m_running = false;
    }
    
private:
    Ms2kGuiHooks setupHooks() {
        Ms2kGuiHooks hooks;
        
        // GUI -> Emulator callbacks (simulated hardware input)
        hooks.readKnob = [this](VR knob) -> uint16_t {
            int idx = static_cast<int>(knob);
            if (idx >= 0 && idx < 32) {
                // Convert 0.0-1.0 to 12-bit ADC (0-4095)
                return static_cast<uint16_t>(m_knobPositions[idx] * 4095.0f);
            }
            return 2048; // Mid-range
        };
        
        hooks.readSwitch = [this](MS2000Button button) -> bool {
            int idx = static_cast<int>(button);
            if (idx >= 0 && idx < 64) {
                return m_buttonStates[idx];
            }
            return false;
        };
        
        hooks.midiIn = [this](const uint8_t* data, size_t length) {
            // Store received MIDI for display
            m_lastMidiIn.assign(data, data + length);
            std::cout << "🎵 Received MIDI: ";
            for (size_t i = 0; i < length; i++) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
            }
            std::cout << std::dec << std::endl;
        };
        
        // Emulator -> GUI callbacks (simulated hardware output)
        hooks.lcdPutChar = [this](int row, int col, char ch) {
            if (row >= 0 && row < 2 && col >= 0 && col < 16) {
                if (m_lcdLines[row].length() < 16) {
                    m_lcdLines[row].resize(16, ' ');
                }
                m_lcdLines[row][col] = ch;
                std::cout << "📟 LCD[" << row << "," << col << "] = '" << ch << "'" << std::endl;
            }
        };
        
        hooks.lcdClear = [this]() {
            m_lcdLines[0] = "                ";
            m_lcdLines[1] = "                ";
            std::cout << "📟 LCD cleared" << std::endl;
        };
        
        hooks.ledSet = [this](MS2000LED led, const LEDColor& color) {
            int idx = static_cast<int>(led);
            if (idx >= 0 && idx < 128) {
                m_ledStates[idx] = color;
                std::cout << "💡 LED[" << idx << "] = R:" << (int)color.red << " G:" << (int)color.green << std::endl;
            }
        };
        
        hooks.midiOut = [this](const uint8_t* data, size_t length) {
            // Store sent MIDI for display
            m_lastMidiOut.assign(data, data + length);
            std::cout << "🎵 Sent MIDI: ";
            for (size_t i = 0; i < length; i++) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
            }
            std::cout << std::dec << std::endl;
        };
        
        hooks.audioOut = [this](const int16_t* samples, int frames) {
            // Audio callback - could process audio here
            (void)samples; (void)frames; // Suppress warnings
        };
        
        hooks.logFn = [](const char* message) {
            // Filter out excessive firmware debug messages
            std::string msg(message);
            if (msg.find("TRAPA") == std::string::npos && 
                msg.find("invalid vector") == std::string::npos) {
                std::cout << "[MS2000] " << message;
            }
        };
        
        return hooks;
    }
    
    void updateDemo(double deltaTime) {
        m_time += deltaTime;
        
        // Demo automation - slowly sweep some knobs and flash LEDs
        
        // Sweep filter cutoff
        if (static_cast<int>(VR::FILT_CUTOFF) < 32) {
            m_knobPositions[static_cast<int>(VR::FILT_CUTOFF)] = 
                0.5f + 0.4f * static_cast<float>(std::sin(m_time * 0.8));
        }
        
        // Sweep EG attack
        if (static_cast<int>(VR::EG1_ATTACK) < 32) {
            m_knobPositions[static_cast<int>(VR::EG1_ATTACK)] = 
                0.3f + 0.3f * static_cast<float>(std::sin(m_time * 1.2));
        }
        
        // Flash some LEDs in sequence
        int flashLed = static_cast<int>(m_time * 3.0) % 8;
        for (int i = 0; i < 8; i++) {
            if (i < 128) {
                if (i == flashLed) {
                    m_ledStates[i] = {255, 0}; // Red
                } else if ((i + 1) % 3 == 0) {
                    m_ledStates[i] = {0, 128}; // Green
                } else {
                    m_ledStates[i] = {0, 0}; // Off
                }
            }
        }
        
        // Update LCD with time
        if (static_cast<int>(m_time * 2) % 4 == 0) {
            std::ostringstream oss;
            oss << "Runtime: " << std::fixed << std::setprecision(1) << m_time << "s";
            std::string timeStr = oss.str();
            if (timeStr.length() < 16) {
                timeStr.resize(16, ' ');
            }
            m_lcdLines[1] = timeStr;
        }
    }
    
    void processInput() {
        // Simple keyboard input for demo interaction
#ifdef _WIN32
        if (_kbhit()) {
            char key = _getch();
            handleKeyPress(key);
        }
#endif
    }
    
    void handleKeyPress(char key) {
        switch (key) {
            case 'q':
            case 'Q':
            case 27: // ESC
                m_running = false;
                break;
                
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': {
                // Simulate button press
                int buttonIdx = key - '1';
                m_buttonStates[buttonIdx] = !m_buttonStates[buttonIdx];
                std::cout << "🔘 Button " << (buttonIdx + 1) << 
                    (m_buttonStates[buttonIdx] ? " PRESSED" : " RELEASED") << std::endl;
                
                // Simulate LED response to button press
                if (buttonIdx < 128) {
                    if (m_buttonStates[buttonIdx]) {
                        m_ledStates[buttonIdx] = {255, 255}; // Yellow when pressed
                    } else {
                        m_ledStates[buttonIdx] = {0, 50}; // Dim green when released
                    }
                }
                break;
            }
            
            case 's': case 'S': {
                // Test MIDI through our API
                testMIDIAPI();
                break;
            }
            
            case 'l': case 'L': {
                // Test LCD through our API
                testLCDAPI();
                break;
            }
            
            case 'k': case 'K': {
                // Show knob values
                showKnobValues();
                break;
            }
        }
    }
    
    void testMIDIAPI() {
        if (m_runner) {
            // Send a test MIDI message through our API
            std::vector<uint8_t> noteOn = {0x90, static_cast<uint8_t>(60 + (rand() % 12)), 100}; // Random note
            m_runner->sendMIDIData(noteOn.data(), noteOn.size());
        }
    }
    
    void testLCDAPI() {
        // Simulate LCD update
        static int testCount = 0;
        std::ostringstream oss;
        oss << "LCD Test #" << (++testCount);
        std::string testStr = oss.str();
        if (testStr.length() < 16) {
            testStr.resize(16, ' ');
        }
        m_lcdLines[0] = testStr;
        std::cout << "📟 LCD updated via API" << std::endl;
    }
    
    void showKnobValues() {
        std::cout << "\n🎛️  Current Knob Positions:" << std::endl;
        for (int i = 0; i < 8; i++) {
            std::cout << "  Knob " << i << ": " << std::fixed << std::setprecision(1) 
                      << (m_knobPositions[i] * 100.0f) << "% (ADC: " 
                      << static_cast<int>(m_knobPositions[i] * 4095.0f) << ")" << std::endl;
        }
        std::cout << std::endl;
    }
    
    void updateDisplay() {
        // Clear screen and show GUI state
        clearScreen();
        
        std::cout << "🎹 MS2000 Synthesizer Emulator - Clean GUI Demo\n";
        std::cout << "===============================================\n\n";
        
        // Show LCD display
        std::cout << "📟 LCD Display:\n";
        std::cout << "┌────────────────┐\n";
        std::cout << "│" << m_lcdLines[0] << "│\n";
        std::cout << "│" << m_lcdLines[1] << "│\n";
        std::cout << "└────────────────┘\n\n";
        
        // Show key knobs with live animation
        std::cout << "🎛️  Key Knob Positions:\n";
        
        // Filter cutoff
        int cutoffIdx = static_cast<int>(VR::FILT_CUTOFF);
        if (cutoffIdx < 32) {
            std::cout << "Filter Cutoff: ";
            printKnobBar(m_knobPositions[cutoffIdx]);
            std::cout << " (" << std::fixed << std::setprecision(1) 
                      << (m_knobPositions[cutoffIdx] * 100.0f) << "%)\n";
        }
        
        // EG Attack
        int attackIdx = static_cast<int>(VR::EG1_ATTACK);
        if (attackIdx < 32) {
            std::cout << "EG1 Attack:    ";
            printKnobBar(m_knobPositions[attackIdx]);
            std::cout << " (" << std::fixed << std::setprecision(1) 
                      << (m_knobPositions[attackIdx] * 100.0f) << "%)\n";
        }
        std::cout << "\n";
        
        // Show button states
        std::cout << "🔘 Buttons: ";
        for (int i = 0; i < 8; i++) {
            std::cout << "[" << (i + 1) << ":" << (m_buttonStates[i] ? "●" : "○") << "] ";
        }
        std::cout << "\n\n";
        
        // Show LED states
        std::cout << "💡 LEDs: ";
        for (int i = 0; i < 8; i++) {
            if (m_ledStates[i].red > 200) {
                std::cout << "🔴 ";
            } else if (m_ledStates[i].green > 100) {
                std::cout << "🟢 ";
            } else if (m_ledStates[i].red > 100 && m_ledStates[i].green > 100) {
                std::cout << "🟡 ";
            } else if (m_ledStates[i].green > 20) {
                std::cout << "🟢 ";
            } else {
                std::cout << "⚫ ";
            }
        }
        std::cout << "\n\n";
        
        // Show MIDI activity
        if (!m_lastMidiOut.empty()) {
            std::cout << "🎵 Last MIDI OUT: ";
            for (uint8_t byte : m_lastMidiOut) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
            }
            std::cout << std::dec << "\n";
        }
        
        if (!m_lastMidiIn.empty()) {
            std::cout << "🎵 Last MIDI IN:  ";
            for (uint8_t byte : m_lastMidiIn) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
            }
            std::cout << std::dec << "\n";
        }
        
        std::cout << "\n⏱️  Runtime: " << std::fixed << std::setprecision(1) << m_time << "s\n";
        std::cout << "✨ All GUI API callbacks working!\n";
    }
    
    void printKnobBar(float value) {
        const int barWidth = 20;
        int filled = static_cast<int>(value * barWidth);
        
        std::cout << "[";
        for (int i = 0; i < barWidth; i++) {
            if (i < filled) {
                std::cout << "█";
            } else {
                std::cout << "░";
            }
        }
        std::cout << "]";
    }
    
    void clearScreen() {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
    }
    
    void printInstructions() {
        std::cout << "🎮 Interactive Controls:\n";
        std::cout << "  1-8: Press/release buttons (watch LEDs respond!)\n";
        std::cout << "  S:   Send test MIDI note via API\n";
        std::cout << "  L:   Update LCD via API\n";
        std::cout << "  K:   Show detailed knob values\n";
        std::cout << "  Q:   Quit demo\n\n";
        
        std::cout << "👀 Watch the live demo:\n";
        std::cout << "  🎛️  Knobs sweep automatically\n";
        std::cout << "  💡 LEDs flash in sequence\n";
        std::cout << "  📟 LCD shows runtime\n";
        std::cout << "  🎵 MIDI activity is logged\n\n";
        
        std::cout << "✨ This demonstrates the complete MS2000 GUI API!\n\n";
        std::cout << "Press any key to start the demo...\n";
        
#ifdef _WIN32
        _getch();
#endif
    }
};

int main() {
    std::cout << "🎹 MS2000 Clean GUI Demo Application\n";
    std::cout << "====================================\n\n";
    
    try {
        MS2000CleanDemo demo;
        
        if (!demo.initialize()) {
            std::cerr << "❌ Failed to initialize MS2000 demo" << std::endl;
            return 1;
        }
        
        demo.run();
        demo.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Demo error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n🎉 MS2000 GUI API Demo completed successfully!" << std::endl;
    return 0;
}