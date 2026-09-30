// fw14.txt: Headless Golden Master test for automated acceptance testing
#include "../core/ms2000_runner.h"
#include "../core/lcd_gui.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>
#include <string>
#include <algorithm>
#include <cstring>

class HeadlessRunner {
public:
    HeadlessRunner() {
        // Configure for headless operation (no ImGui, no audio output)
        MS2000::Ms2kConfig config;
        config.romPath = "flash.bin";
        config.romBase = 0x000000;
        config.vbr = 0x000000;
        config.cpuCyclesPerTick = 20000;
        config.sampleRate = 48000;
        
        // Setup hooks for LCD capture
        MS2000::Ms2kGuiHooks hooks;
        hooks.lcdPutChar = [this](int row, int col, char ch) {
            if (row >= 0 && row < 2 && col >= 0 && col < 16) {
                m_lcdBuffer[row][col] = ch;
            }
        };
        hooks.lcdClear = [this]() {
            memset(m_lcdBuffer, ' ', sizeof(m_lcdBuffer));
        };
        hooks.logFn = [this](const char* msg) {
            m_logMessages.push_back(std::string(msg));
        };
        
        m_runner = std::make_unique<MS2000::Ms2kRunner>(config, hooks);
        
        // Clear LCD buffer
        memset(m_lcdBuffer, ' ', sizeof(m_lcdBuffer));
        for (int i = 0; i < 2; i++) {
            m_lcdBuffer[i][16] = '\0';  // Null terminate
        }
    }
    
    bool start(int durationMs) {
        if (!m_runner->init()) {
            std::cerr << "Failed to initialize MS2000 runner" << std::endl;
            return false;
        }
        
        std::cout << "Starting headless MS2000 emulation for " << durationMs << "ms..." << std::endl;
        
        m_runner->start();
        
        // Wait for specified duration
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
        
        m_runner->stop();
        
        return true;
    }
    
    LcdGuiSnapshot getLcdGuiSnapshot() const {
        // Get snapshot from the runner's LCD GUI buffer
        if (m_runner) {
            return m_runner->getLcdGuiSnapshot();
        }
        
        // Fallback: create snapshot from our buffer
        LcdGuiSnapshot snapshot;
        snapshot.displayOn = true;
        snapshot.cursorOn = false;
        snapshot.blinkOn = false;
        snapshot.curLine = 0;
        snapshot.curCol = 0;
        
        std::memcpy(snapshot.line0, m_lcdBuffer[0], 16);
        std::memcpy(snapshot.line1, m_lcdBuffer[1], 16);
        snapshot.line0[16] = '\0';
        snapshot.line1[16] = '\0';
        
        // Remove trailing spaces for cleaner comparison
        std::string line0_str(snapshot.line0);
        std::string line1_str(snapshot.line1);
        while (!line0_str.empty() && line0_str.back() == ' ') {
            line0_str.pop_back();
        }
        while (!line1_str.empty() && line1_str.back() == ' ') {
            line1_str.pop_back();
        }
        std::memcpy(snapshot.line0, line0_str.c_str(), std::min<size_t>(16, line0_str.length()));
        std::memcpy(snapshot.line1, line1_str.c_str(), std::min<size_t>(16, line1_str.length()));
        
        return snapshot;
    }
    
    void dumpLcdContent() const {
        auto snapshot = getLcdGuiSnapshot();
        std::cout << "=== LCD Content ===\n";
        std::cout << "[" << std::string(snapshot.line0) << "]\n";
        std::cout << "[" << std::string(snapshot.line1) << "]\n";
        std::cout << "Display ON: " << (snapshot.displayOn ? "YES" : "NO") << "\n";
        std::cout << "Cursor: Line=" << snapshot.curLine << ", Col=" << snapshot.curCol << "\n";
    }
    
    void dumpLogMessages() const {
        std::cout << "=== Log Messages (last 10) ===\n";
        size_t start = m_logMessages.size() > 10 ? m_logMessages.size() - 10 : 0;
        for (size_t i = start; i < m_logMessages.size(); i++) {
            std::cout << m_logMessages[i] << "\n";
        }
    }

private:
    std::unique_ptr<MS2000::Ms2kRunner> m_runner;
    char m_lcdBuffer[2][17];  // 2 lines, 16 chars + null terminator
    std::vector<std::string> m_logMessages;
};

// Golden Master Test: Verify KORG MS2000 boot sequence
bool testKorgBootSequence() {
    std::cout << "\n=== Golden Master Test: KORG Boot Sequence ===\n";
    
    HeadlessRunner runner;
    
    // Run for 2 seconds - should be enough for LCD initialization
    if (!runner.start(2000)) {
        std::cerr << "❌ Failed to start emulation\n";
        return false;
    }
    
    auto snapshot = runner.getLcdGuiSnapshot();
    
    // Dump current state for debugging
    runner.dumpLcdContent();
    
    // Test 1: Display should be ON
    if (!snapshot.displayOn) {
        std::cerr << "❌ Display not ON\n";
        return false;
    }
    
    // Test 2: Cursor should be positioned (>= 0)
    if (snapshot.curLine < 0) {
        std::cerr << "❌ Invalid cursor line position: " << snapshot.curLine << "\n";
        return false;
    }
    
    // Test 3: Should contain KORG text (case insensitive)
    std::string line0 = snapshot.line0;
    std::string line1 = snapshot.line1;
    std::string combined = line0 + " " + line1;
    
    // Convert to uppercase for comparison
    std::transform(combined.begin(), combined.end(), combined.begin(), ::toupper);
    
    bool hasKorg = combined.find("KORG") != std::string::npos;
    bool hasMs2000 = combined.find("MS2000") != std::string::npos || 
                     combined.find("MS-2000") != std::string::npos ||
                     combined.find("MS 2000") != std::string::npos;
    
    if (!hasKorg && !hasMs2000) {
        std::cerr << "❌ No KORG or MS2000 text found in LCD output\n";
        std::cerr << "   Combined text: '" << combined << "'\n";
        runner.dumpLogMessages();
        return false;
    }
    
    // Test 4: Should have some non-empty content
    if (line0.empty() && line1.empty()) {
        std::cerr << "❌ LCD is completely empty\n";
        return false;
    }
    
    std::cout << "✅ Golden Master Test PASSED!\n";
    std::cout << "   Found: " << (hasKorg ? "KORG " : "") << (hasMs2000 ? "MS2000" : "") << "\n";
    
    return true;
}

// Performance test: Verify emulation runs without crashes
bool testEmulationStability() {
    std::cout << "\n=== Stability Test: 5 Second Run ===\n";
    
    HeadlessRunner runner;
    
    // Run for 5 seconds
    if (!runner.start(5000)) {
        std::cerr << "❌ Failed to start emulation\n";
        return false;
    }
    
    auto snapshot = runner.getLcdGuiSnapshot();
    runner.dumpLcdContent();
    
    // Basic sanity checks
    if (!snapshot.displayOn) {
        std::cerr << "❌ Display not stable\n";
        return false;
    }
    
    std::cout << "✅ Stability Test PASSED!\n";
    return true;
}

// Quick smoke test: Just verify it starts
bool testQuickSmoke() {
    std::cout << "\n=== Quick Smoke Test: 500ms Run ===\n";
    
    HeadlessRunner runner;
    
    // Run for just 500ms
    if (!runner.start(500)) {
        std::cerr << "❌ Failed to start emulation\n";
        return false;
    }
    
    std::cout << "✅ Quick Smoke Test PASSED!\n";
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "fw14.txt Headless Golden Master Test Suite\n";
    std::cout << "==========================================\n";
    
    bool allPassed = true;
    
    // Parse arguments
    bool runFull = (argc > 1 && std::string(argv[1]) == "--full");
    bool runQuick = (argc > 1 && std::string(argv[1]) == "--quick");
    
    if (runQuick) {
        // CI/CD friendly quick test
        allPassed &= testQuickSmoke();
    } else if (runFull) {
        // Full test suite
        allPassed &= testQuickSmoke();
        allPassed &= testKorgBootSequence();
        allPassed &= testEmulationStability();
    } else {
        // Default: just the Golden Master test
        allPassed &= testKorgBootSequence();
    }
    
    std::cout << "\n==========================================\n";
    if (allPassed) {
        std::cout << "✅ ALL TESTS PASSED!\n";
        return 0;
    } else {
        std::cout << "❌ SOME TESTS FAILED!\n";
        return 1;
    }
}