// fw15.txt: Headless runner for Golden Master testing
#pragma once
#include "ms2000_runner.h"
#include "lcd_gui.h"
#include <chrono>
#include <thread>
#include <vector>
#include <string>
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
        // BUG100: the Golden Master ran with the per-instruction trace ON - four
        // printf lines per instruction - because nothing here set quietBoot and the
        // default is false. Measured 2026-09-23: 9,553 instructions in its 2-second
        // window, when the firmware needs ~250,000 to reach its splash. An
        // instrument on the gate's own path starved the gate (R2: diagnostics
        // default OFF - and the S3000XL lesson already written into lcd_trace.cpp).
        config.quietBoot = true;
        
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
    
    bool init() {
        return m_runner->init();
    }
    
    bool start(int durationMs) {
        // BUG69: this used to call m_runner->init() a SECOND time - runSelftest()
        // has already called HeadlessRunner::init() by the time it gets here, and
        // the re-entry is what made --selftest die in std::terminate immediately
        // after the TRAPA/RTE test passed. init() is now idempotent as well, but
        // the redundant call goes too: a boot sequence is not a no-op.
        m_runner->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
        m_runner->stop();
        
        return true;
    }
    
    // BUG100: wait for a CONDITION, not for a wall-clock time. A fixed sleep makes
    // the gate a measurement of how fast this PC is. Start, poll the display every
    // 50 ms, stop as soon as `done` holds or `timeoutMs` passes. Returns the
    // elapsed milliseconds, or -1 on timeout. The condition is only ever satisfied
    // by what the firmware itself wrote into the display buffer.
    template <class Pred>
    int startUntil(Pred done, int timeoutMs) {
        using clk = std::chrono::steady_clock;
        const auto t0 = clk::now();
        m_runner->start();
        int elapsed = -1;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count());
            if (done(m_runner->getLcdGuiSnapshot())) { elapsed = ms; break; }
            if (ms >= timeoutMs) break;
        }
        m_lastSnapshot = m_runner->getLcdGuiSnapshot();   // taken BEFORE stop() tears the CPU down
        m_runner->stop();
        return elapsed;
    }
    const LcdGuiSnapshot& lastSnapshot() const { return m_lastSnapshot; }

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
        
        strncpy_s(snapshot.line0, sizeof(snapshot.line0), m_lcdBuffer[0], 16);
        strncpy_s(snapshot.line1, sizeof(snapshot.line1), m_lcdBuffer[1], 16);
        snapshot.line0[16] = '\0';
        snapshot.line1[16] = '\0';
        
        return snapshot;
    }
    
    // TRAPA→RTE selftest access
    MS2000::H8S2350Emulator& getEmulator() const {
        return m_runner->getEmulator();
    }
    
private:
    std::unique_ptr<MS2000::Ms2kRunner> m_runner;
    char m_lcdBuffer[2][17];  // 2 lines, 16 chars + null terminator
    std::vector<std::string> m_logMessages;
    LcdGuiSnapshot m_lastSnapshot{};
};