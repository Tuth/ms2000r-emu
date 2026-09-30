// Minimal headless MS2000 main to verify firmware load and boot path via Ms2kRunner
#include "../core/ms2000_runner.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace MS2000;

int main(int argc, char** argv) {
    std::string rom = "flash.bin";
    if (argc > 1) rom = argv[1];

    Ms2kConfig cfg;
    cfg.romPath = rom;
    cfg.cpuCyclesPerTick = 20000; // 1ms tick pacing
    cfg.sampleRate = 48000;

    bool sawLog = false;
    Ms2kGuiHooks hooks;
    hooks.logFn = [&](const char* s){
        std::cout << s; 
        sawLog = true;
    };
    hooks.lcdClear = [&](){ std::cout << "[LCD] clear\n"; };
    hooks.lcdPutChar = [&](int row, int col, char ch){
        std::cout << "[LCD] (" << row << "," << col << ") '" << (ch?ch:' ') << "'\n";
    };

    std::cout << "[MAIN] Booting MS2000 via Ms2kRunner\n";
    std::cout << "[MAIN] ROM: " << cfg.romPath << "\n";

    Ms2kRunner runner(cfg, hooks);
    if (!runner.init()) {
        std::cerr << "[MAIN] ERROR: init failed (ROM load / mapping)\n";
        return 1;
    }

    std::cout << "[MAIN] init OK, starting threads...\n";
    runner.start();

    // Let it run briefly to see logs/LCD activity
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "[MAIN] stopping...\n";
    runner.stop();

    std::cout << "[MAIN] done. Logs seen: " << (sawLog?"yes":"no") << "\n";
    return 0;
}

