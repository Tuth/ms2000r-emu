#pragma once
#include "../core/ms2000_runner.h"
#include "../core/ms2000_switch_matrix.h"
#include "../core/ms2000_led_matrix.h"

// Example GUI API usage for MS2000 emulator
// This demonstrates how to integrate the MS2000 emulator with a GUI framework

namespace MS2000 {

class MS2000GuiExample {
public:
    MS2000GuiExample() {
        // Configure emulator
        Ms2kConfig config;
        config.romPath = "x811v107.sys";
        config.cpuCyclesPerTick = 20000;
        config.sampleRate = 48000;
        
        // Set up GUI hooks
        Ms2kGuiHooks hooks;
        
        // GUI -> Emulator callbacks (input)
        hooks.readKnob = [this](VR knob) -> uint16_t {
            return this->onReadKnob(knob);
        };
        
        hooks.readSwitch = [this](MS2000Button button) -> bool {
            return this->onReadButton(button);
        };
        
        hooks.midiIn = [this](const uint8_t* data, size_t length) {
            this->onMidiInput(data, length);
        };
        
        // Emulator -> GUI callbacks (output)
        hooks.lcdPutChar = [this](int row, int col, char ch) {
            this->onLcdChar(row, col, ch);
        };
        
        hooks.lcdClear = [this]() {
            this->onLcdClear();
        };
        
        hooks.ledSet = [this](MS2000LED led, LEDColor color, bool state) {
            this->onLedUpdate(led, color, state);
        };
        
        hooks.midiOut = [this](const uint8_t* data, size_t length) {
            this->onMidiOutput(data, length);
        };
        
        hooks.audioOut = [this](const int16_t* samples, int frames) {
            this->onAudioOutput(samples, frames);
        };
        
        hooks.logFn = [this](const char* message) {
            this->onLogMessage(message);
        };
        
        // Create runner with configuration and hooks
        m_runner = std::make_unique<Ms2kRunner>(config, hooks);
    }
    
    // Initialize and start the emulator
    bool start() {
        if (!m_runner->init()) {
            return false;
        }
        m_runner->start();
        return true;
    }
    
    void stop() {
        if (m_runner) {
            m_runner->stop();
        }
    }
    
    // GUI API methods - call these from your GUI event handlers
    
    void setKnobValue(VR knob, float normalizedValue) {
        // Convert 0.0-1.0 to 12-bit ADC range
        uint16_t adcValue = static_cast<uint16_t>(normalizedValue * 4095.0f);
        m_runner->setKnobValue(knob, adcValue);
    }
    
    void buttonPressed(MS2000Button button) {
        m_runner->pressButton(button);
    }
    
    void buttonReleased(MS2000Button button) {
        m_runner->releaseButton(button);
    }
    
    void sendMidiMessage(const std::vector<uint8_t>& message) {
        m_runner->sendMIDIData(message.data(), message.size());
    }
    
    // Query current state
    float getKnobPosition(VR knob) const {
        uint16_t adcValue = m_runner->getKnobValue(knob);
        return static_cast<float>(adcValue) / 4095.0f; // Convert to 0.0-1.0
    }
    
    bool isButtonPressed(MS2000Button button) const {
        return m_runner->isButtonPressed(button);
    }
    
    bool isLEDOn(MS2000LED led, LEDColor color = LEDColor::RED) const {
        return m_runner->isLEDOn(led, color);
    }
    
    std::string getLCDText() const {
        return m_runner->getLCDContent();
    }

private:
    std::unique_ptr<Ms2kRunner> m_runner;
    
    // GUI callback implementations - override these in your GUI
    
    uint16_t onReadKnob(VR knob) {
        // GUI should read actual knob position here
        // For example, from GUI sliders/knobs
        return m_runner->getKnobValue(knob); // Use cached value
    }
    
    bool onReadButton(MS2000Button button) {
        // GUI should read actual button state here
        return m_runner->isButtonPressed(button); // Use cached value
    }
    
    void onMidiInput(const uint8_t* data, size_t length) {
        // Handle MIDI input from external sources if needed
        // This is typically used for MIDI routing
    }
    
    void onLcdChar(int row, int col, char ch) {
        // Update GUI LCD display at position (row, col) with character ch
        printf("LCD[%d,%d] = '%c'\n", row, col, ch);
    }
    
    void onLcdClear() {
        // Clear GUI LCD display
        printf("LCD cleared\n");
    }
    
    void onLedUpdate(MS2000LED led, LEDColor color, bool state) {
        // Update GUI LED display
        const char* ledName = "Unknown";
        const char* colorName = (color == LEDColor::RED) ? "RED" : 
                               (color == LEDColor::GREEN) ? "GREEN" : "ORANGE";
        
        printf("LED %s %s: %s\n", ledName, colorName, state ? "ON" : "OFF");
    }
    
    void onMidiOutput(const uint8_t* data, size_t length) {
        // Send MIDI data to external MIDI interface
        printf("MIDI OUT: ");
        for (size_t i = 0; i < length; i++) {
            printf("0x%02X ", data[i]);
        }
        printf("\n");
    }
    
    void onAudioOutput(const int16_t* samples, int frames) {
        // Send audio to sound card/audio interface
        // samples are interleaved stereo (L,R,L,R,...)
        static int audioCallCount = 0;
        if (++audioCallCount % 100 == 0) {
            printf("Audio: %d frames\n", frames);
        }
    }
    
    void onLogMessage(const char* message) {
        printf("[MS2000] %s", message);
    }
};

} // namespace MS2000

/* Usage Example:

int main() {
    MS2000::MS2000GuiExample gui;
    
    if (!gui.start()) {
        printf("Failed to start MS2000 emulator\n");
        return 1;
    }
    
    // Simulate some GUI interactions
    gui.setKnobValue(MS2000::VR::FILTER_CUTOFF, 0.75f); // 75% position
    gui.buttonPressed(MS2000::MS2000Button::PROG_UP);
    
    // Wait for some time...
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    gui.buttonReleased(MS2000::MS2000Button::PROG_UP);
    
    // Check LED states
    bool ledOn = gui.isLEDOn(MS2000::MS2000LED::PROG_UP_LED);
    printf("PROG_UP LED is %s\n", ledOn ? "ON" : "OFF");
    
    // Send MIDI note
    std::vector<uint8_t> noteOn = {0x90, 60, 127}; // Note On C4, velocity 127
    gui.sendMidiMessage(noteOn);
    
    gui.stop();
    return 0;
}

*/