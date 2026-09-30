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

class MS2000GuiDemo {
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
    int m_demoStep = 0;
    
public:
    MS2000GuiDemo() {
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
        m_lcdLines[0] = "MS2000 Ready    ";
        m_lcdLines[1] = "GUI Demo v1.0   ";
    }
    
    bool initialize() {
        std::cout << "🎹 MS2000 GUI Demo Starting..." << std::endl;
        
        // Configure emulator
        Ms2kConfig config;
        config.romPath = "x811v107.sys"; // Will try to load ROM
        config.cpuCyclesPerTick = 20000;
        config.sampleRate = 48000;
        
        // Set up GUI hooks
        Ms2kGuiHooks hooks = setupHooks();
        
        // Create runner
        m_runner = std::make_unique<Ms2kRunner>(config, hooks);
        
        try {
            if (m_runner->init()) {
                std::cout << "✅ MS2000 initialized with ROM" << std::endl;
                m_runner->start();
            } else {
                std::cout << "⚠️  ROM not found, continuing without firmware" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "⚠️  ROM load failed: " << e.what() << std::endl;
            std::cout << "📋 Continuing in GUI demo mode..." << std::endl;
        }
        
        return true;
    }
    
    void run() {
        m_running = true;
        std::cout << "\n🎛️  MS2000 GUI Demo Running!" << std::endl;
        std::cout << "=====================================\n" << std::endl;
        
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
        
        std::cout << "\n👋 MS2000 GUI Demo Stopped" << std::endl;
    }
    
    void stop() {
        m_running = false;
        if (m_runner) {
            m_runner->stop();
        }
    }
    
private:
    Ms2kGuiHooks setupHooks() {
        Ms2kGuiHooks hooks;
        
        // GUI -> Emulator callbacks
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
            // Store for display
            m_lastMidiIn.assign(data, data + length);
        };
        
        // Emulator -> GUI callbacks
        hooks.lcdPutChar = [this](int row, int col, char ch) {
            if (row >= 0 && row < 2 && col >= 0 && col < 16) {
                if (m_lcdLines[row].length() < 16) {
                    m_lcdLines[row].resize(16, ' ');
                }
                m_lcdLines[row][col] = ch;
            }
        };
        
        hooks.lcdClear = [this]() {
            m_lcdLines[0] = "                ";
            m_lcdLines[1] = "                ";
        };
        
        hooks.ledSet = [this](MS2000LED led, const LEDColor& color) {
            int idx = static_cast<int>(led);
            if (idx >= 0 && idx < 128) {
                m_ledStates[idx] = color;
            }
        };
        
        hooks.midiOut = [this](const uint8_t* data, size_t length) {
            // Store for display
            m_lastMidiOut.assign(data, data + length);
        };
        
        hooks.audioOut = [this](const int16_t* samples, int frames) {
            // Could visualize audio here
            static int audioCallCount = 0;
            if (++audioCallCount % 1000 == 0) {
                // Occasional audio activity indicator
            }
        };
        
        hooks.logFn = [](const char* message) {
            std::cout << "[MS2000] " << message;
        };
        
        return hooks;
    }
    
    void updateDemo(double deltaTime) {
        m_time += deltaTime;
        
        // Demo automation - slowly sweep some knobs and flash LEDs
        
        // Sweep filter cutoff
        if (static_cast<int>(VR::FILT_CUTOFF) < 32) {
            m_knobPositions[static_cast<int>(VR::FILT_CUTOFF)] = 
                0.5f + 0.4f * static_cast<float>(std::sin(m_time * 0.5));
        }
        
        // Sweep LFO rate
        if (static_cast<int>(VR::LFO1_FREQ) < 32) {
            m_knobPositions[static_cast<int>(VR::LFO1_FREQ)] = 
                0.3f + 0.3f * static_cast<float>(std::sin(m_time * 0.7));
        }
        
        // Flash some LEDs
        int flashLed = static_cast<int>(m_time * 2.0) % 8;
        for (int i = 0; i < 8; i++) {
            if (i < 128) {
                if (i == flashLed) {
                    m_ledStates[i] = {255, 0}; // Red
                } else {
                    m_ledStates[i] = {0, 100}; // Dim green
                }
            }
        }
        
        // Demo MIDI output every few seconds
        if (static_cast<int>(m_time) % 3 == 0 && static_cast<int>(m_time * 10) % 10 == 0) {
            sendDemoMIDI();
        }
    }
    
    void sendDemoMIDI() {
        if (m_runner) {
            // Send a note on message
            std::vector<uint8_t> noteOn = {0x90, 60, 100}; // Note On C4, velocity 100
            m_runner->sendMIDIData(noteOn.data(), noteOn.size());
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
                break;
            }
            
            case 's': case 'S': {
                // Send test MIDI
                sendDemoMIDI();
                std::cout << "🎵 Sent demo MIDI note" << std::endl;
                break;
            }
        }
    }
    
    void updateDisplay() {
        // Clear screen and show GUI state
        clearScreen();
        
        std::cout << "🎹 MS2000 Synthesizer Emulator - GUI Demo\n";
        std::cout << "==========================================\n\n";
        
        // Show LCD display
        std::cout << "📟 LCD Display:\n";
        std::cout << "┌────────────────┐\n";
        std::cout << "│" << m_lcdLines[0] << "│\n";
        std::cout << "│" << m_lcdLines[1] << "│\n";
        std::cout << "└────────────────┘\n\n";
        
        // Show some knobs
        std::cout << "🎛️  Knob Positions:\n";
        for (int i = 0; i < 8; i++) {
            if (i < 32) {
                std::cout << "VR" << std::setw(2) << i << ": ";
                printKnobBar(m_knobPositions[i]);
                std::cout << " (" << std::fixed << std::setprecision(1) 
                          << (m_knobPositions[i] * 100.0f) << "%)\n";
            }
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
            if (m_ledStates[i].red > 100) {
                std::cout << "🔴 ";
            } else if (m_ledStates[i].green > 50) {
                std::cout << "🟢 ";
            } else {
                std::cout << "⚫ ";
            }
        }
        std::cout << "\n\n";
        
        // Show MIDI activity
        if (!m_lastMidiOut.empty()) {
            std::cout << "🎵 MIDI OUT: ";
            for (uint8_t byte : m_lastMidiOut) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
            }
            std::cout << std::dec << "\n";
        }
        
        if (!m_lastMidiIn.empty()) {
            std::cout << "🎵 MIDI IN:  ";
            for (uint8_t byte : m_lastMidiIn) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
            }
            std::cout << std::dec << "\n";
        }
        
        std::cout << "\n⏱️  Runtime: " << std::fixed << std::setprecision(1) << m_time << "s\n";
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
        std::cout << "🎮 Controls:\n";
        std::cout << "  1-8: Press/release buttons\n";
        std::cout << "  S:   Send test MIDI note\n";
        std::cout << "  Q:   Quit demo\n\n";
        std::cout << "👀 Watch the knobs sweep automatically!\n";
        std::cout << "💡 LEDs will flash in sequence\n";
        std::cout << "🎵 MIDI activity will be shown\n\n";
        std::cout << "Press any key to continue...\n";
        
#ifdef _WIN32
        _getch();
#endif
    }
};

int main() {
    std::cout << "🎹 MS2000 GUI Demo Application\n";
    std::cout << "===============================\n\n";
    
    try {
        MS2000GuiDemo demo;
        
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
    
    return 0;
}