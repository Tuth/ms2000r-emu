// fw17.txt: Diagnostic panel test program
// Tests the diagnostic state tracking and displays current system status
#include "../core/ms2000_runner.h"
#include "../core/diag_panel.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace MS2000;

void displayDiagnosticSnapshot(const DiagSnapshot& snapshot) {
    std::cout << "\n=== MS2000 Diagnostic Report ===\n";
    
    // Panel source and mode
    const char* srcNames[] = {"Unknown", "SCI0", "SCI1", "SCI2", "HPI"};
    const char* modeNames[] = {"Probe", "LenPref", "Header", "RawLCD"};
    const char* hpiNames[] = {"Auto", "HiLo", "LoHi"};
    
    std::cout << "Panel Source: " << srcNames[static_cast<int>(snapshot.src)] 
              << " | Mode: " << modeNames[static_cast<int>(snapshot.mode)]
              << " | HPI Order: " << hpiNames[static_cast<int>(snapshot.hpiOrder)] << "\n";
    
    // Recording/Replay status
    if (snapshot.recording || snapshot.replaying) {
        std::cout << "Recording: " << (snapshot.recording ? "ON" : "OFF")
                  << " | Replaying: " << (snapshot.replaying ? "ON" : "OFF");
        if (snapshot.deterministicSeed != 0) {
            std::cout << " | Seed: " << snapshot.deterministicSeed;
        }
        std::cout << "\n";
    }
    
    // PanelMP statistics
    std::cout << "PanelMP: bytes=" << snapshot.mpBytes 
              << " frames=" << snapshot.mpFrames << "\n";
    
    // SCI channels
    std::cout << "\nSCI Channels:\n";
    for (int i = 0; i < 3; i++) {
        const auto& sci = snapshot.sci[i];
        if (sci.present) {
            std::cout << "  SCI" << i << ": present, baud=" << sci.baud
                      << ", " << sci.dataBits << sci.parity << sci.stopBits
                      << ", TE=" << (sci.txEnabled ? 1 : 0)
                      << ", CM=" << (sci.syncMode ? 1 : 0)
                      << ", TX=" << sci.txBytes << "\n";
        } else {
            std::cout << "  SCI" << i << ": not present\n";
        }
    }
    
    // LCD Boot Probe
    std::cout << "\nLCD Boot Probe:\n";
    std::cout << "  FN=" << (snapshot.boot.fn ? 1 : 0)
              << " ON=" << (snapshot.boot.on ? 1 : 0)
              << " CLR=" << (snapshot.boot.clr ? 1 : 0)
              << " ENT=" << (snapshot.boot.ent ? 1 : 0)
              << " DDRAM=" << (snapshot.boot.ddram ? 1 : 0)
              << " DATA=" << snapshot.boot.dataCount << "\n";
    
    // Boot progress
    int bootSteps = 0;
    if (snapshot.boot.fn) bootSteps++;
    if (snapshot.boot.on) bootSteps++;
    if (snapshot.boot.clr) bootSteps++;
    if (snapshot.boot.ent) bootSteps++;
    if (snapshot.boot.ddram) bootSteps++;
    
    std::cout << "  Boot Progress: " << bootSteps << "/5 steps completed ("
              << std::fixed << std::setprecision(1) << (bootSteps * 20.0f) << "%)\n";
    
    // Deterministic timing
    if (snapshot.tickCount > 0) {
        std::cout << "\nDeterministic State:\n";
        std::cout << "  Tick Count: " << snapshot.tickCount 
                  << " (" << std::fixed << std::setprecision(3) << (snapshot.tickCount / 1000.0) << "s)\n";
    }
    
    // Notes
    if (!snapshot.notes.empty()) {
        std::cout << "\nNotes: " << snapshot.notes << "\n";
    }
    
    std::cout << "================================\n";
}

int main() {
    std::cout << "fw17.txt: MS2000 Diagnostic Panel Test\n";
    std::cout << "Testing real-time diagnostic state tracking...\n";
    
    // Configure the emulator for diagnostic testing
    Ms2kConfig config;
    config.romPath = "flash.bin";
    config.cpuCyclesPerTick = 20000;
    config.sampleRate = 48000;
    config.deterministicConfig.fixedSeed = true;
    config.deterministicConfig.prngSeed = 54321;
    config.deterministicConfig.deterministicTiming = true;
    
    Ms2kGuiHooks hooks;
    hooks.logFn = [](const char* s) { 
        // Suppress most log output for cleaner diagnostic display
        std::string msg(s);
        if (msg.find("[DIAG]") != std::string::npos || 
            msg.find("LCD-BOOT") != std::string::npos ||
            msg.find("PanelMP") != std::string::npos) {
            std::cout << s;
        }
    };
    
    hooks.lcdClear = []() { 
        std::cout << "[DIAG] LCD cleared\n";
    };
    
    hooks.lcdPutChar = [](int row, int col, char ch) {
        std::cout << "[DIAG] LCD[" << row << "," << col << "] = '" << ch << "'\n";
    };
    
    Ms2kRunner runner(config, hooks);
    
    if (!runner.init()) {
        std::cerr << "ERROR: Failed to initialize MS2000 runner\n";
        return 1;
    }
    
    std::cout << "\nStarting MS2000 emulator for diagnostic monitoring...\n";
    runner.start();
    
    // Monitor diagnostic state for 5 seconds with periodic snapshots
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Update deterministic tick count
        runner.getDeterministicState().advanceTick();
        
        // Get diagnostic snapshot
        auto diagSnapshot = runner.getDiagnosticState().snapshot();
        
        std::cout << "\n--- Diagnostic Snapshot " << (i + 1) << "/5 ---";
        displayDiagnosticSnapshot(diagSnapshot);
        
        // Update diagnostic state with current tick count
        runner.getDiagnosticState().setDeterministicState(
            runner.getDeterministicState().getCurrentSeed(),
            runner.getDeterministicState().getTickCount()
        );
    }
    
    std::cout << "\nStopping MS2000 emulator...\n";
    runner.stop();
    
    std::cout << "✅ Diagnostic panel test completed successfully!\n";
    return 0;
}