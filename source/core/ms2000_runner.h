#pragma once
#include <functional>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

// Include actual enum definitions
#include "ms2000_switch_matrix.h"  // For MS2000Button
#include "ms2000_led_matrix.h"     // For MS2000LED and LEDColor
#include "ms2000_panel_adc.h"      // For VR enum
// LCD wiring
#include "real_lcd_display.h"
#include "h8s_lcd_adapter.h"
// fw15.txt: Deterministic state and recording
#include "deterministic_state.h"
#include "panel_recorder.h"
// fw17.txt: Diagnostic panel system
#include "diag_panel.h"
// fw18.txt: MMCSS audio thread support
#include "../audio/audio_thread.h"
// fw19.txt: I²S/AK4522 audio pipeline
#include "../audio/i2s_ak4522.h"
#include "../audio/dsp_audio_adapter.h"
#include "../audio/audio_engine.h"
// fw23.txt: Panel I/O validation system
#include "panel_io_validator.h"
// fw27.txt Phase 3: State snapshot system
#include "state_snapshot.h"
// fw27.txt Phase 4: Long-run soak testing
#include "soak_test_monitor.h"
// fw27.txt Phase 5: Panel timeline & LCD diff visualization
#include "panel_timeline_visualizer.h"

// Forward declarations
class PanelMP;
class LcdGuiBuffer;
struct LcdGuiSnapshot;
struct LcdBootProbe;

// Forward declarations for existing cores
namespace MS2000 {
    class H8S2350Emulator;
    class DSP56362Emulator;
}

namespace MS2000 {

struct Ms2kGuiHooks {
    // GUI -> Emu (input callbacks)
    std::function<uint16_t(VR)> readKnob = nullptr;                    // Read potentiometer value (0-4095)
    std::function<bool(MS2000Button)> readSwitch = nullptr;            // Read button state (pressed/released)
    std::function<void(const uint8_t*, size_t)> midiIn = nullptr;      // Send MIDI data to synth
    
    // Emu -> GUI (output callbacks) 
    std::function<void(int row, int col, char ch)> lcdPutChar = nullptr;      // Display character on LCD
    std::function<void()> lcdClear = nullptr;                                 // Clear LCD display
    std::function<void(MS2000LED, const LEDColor&)> ledSet = nullptr;          // Set LED state and color
    std::function<void(const uint8_t*, size_t)> midiOut = nullptr;            // Receive MIDI data from synth
    std::function<void(const int16_t*, int)> audioOut = nullptr;              // Receive audio samples
    std::function<void(const char*)> logFn = nullptr;                         // Debug logging
};

struct Ms2kConfig {
    std::string romPath = "x811v107.sys";
    uint32_t    romBase = 0x000000;  // ROM_BASE
    uint32_t    vbr     = 0x000000;  // TRAPA/IRQ vectors
    int         cpuCyclesPerTick = 20000;   // per 1ms tick
    
    // fw15.txt: Deterministic execution configuration
    DeterministicState::Config deterministicConfig;
    int         sampleRate = 48000;         // 48kHz
    
    // fw18.txt: MMCSS audio thread priority
    bool        useMmcss = false;           // Enable Windows MMCSS Pro Audio
    
    // fw19.txt: I²S/AK4522 audio pipeline configuration
    struct AudioConfig {
        bool enabled = false;               // --audio
        uint32_t sampleRate = 48000;        // --sample-rate 48000
        uint32_t bufferFrames = 256;        // --buffer-frames 256
        std::string mode = "host";          // --audio-mode host|pll
    } audio;
    
    // fw21.txt: Crash dump configuration
    std::string crashDumpsDir = "dumps";    // --crash-dumps-dir
    
    // i15.txt: MIDI smoke test configuration
    bool enableMidiSmokeTest = true;        // --midi-smoke-test (temporarily enabled for testing)

    // lcd5.txt: CPU trace configuration to prevent muting after I/O scan
    bool traceCpuAlways = false;            // --trace-cpu=always (keep CPU traces after I/O scan)

    // quiet boot - suppress per-instruction verbose trace for fast boot
    bool quietBoot = false;                 // --quiet-boot

    // VST3-2: where the flash state (BUG126) comes from and goes to. The standalone uses the files beside
    // the ROM; the plugin keeps it in the host project: flashStateMask != 0 -> those sectors are taken from
    // flashStateImage (FLASH_SIZE bytes) instead of the file. MS2K_FLASHSTATE=off still overrides all of it.
    bool        flashStateLoad = true;      // take ms2000_flash_state.bin/.sectors at init
    bool        flashStateSave = true;      // write them at stop
    uint32_t    flashStateMask = 0;
    std::vector<uint8_t> flashStateImage;
};

class Ms2kRunner {
public:
    explicit Ms2kRunner(const Ms2kConfig& cfg, const Ms2kGuiHooks& hooks);
    ~Ms2kRunner();

    bool init();      // ROM load, map, hooks, reset sequence
    void start();     // threads start (CPU + audio)
    void stop();      // clean shutdown
    bool ready() const { return m_ready; }

    // ENGINE-BLOCK (2026-09-28, VST3 phase 0): the machine driven by its caller, with no threads of its own.
    // startManual() does what start() does except start the CPU thread; render() then runs the same 1 ms ticks
    // the thread runs (tick1ms) until the DSP has produced the frames asked for, and hands them over in order.
    // MIDI events are delivered to the MIDI IN line at their frame offset within the block (to the tick).
    struct MidiEv { uint32_t frame; uint8_t len; uint8_t data[3]; };
    bool startManual();
    uint32_t render(float* left, float* right, uint32_t frames, const MidiEv* events = nullptr, size_t nEvents = 0, uint8_t* fromDsp = nullptr);
    bool tick1ms();                  // one 1 ms tick of the machine: the CPU steps and the runner's per-tick work
    bool runSteps(int n);          // PERF-VST: n step() calls, tick1ms housekeeping every cpuCyclesPerTick steps
    bool tickHousekeeping();       // what tick1ms() does after its steps
    static constexpr int kRenderSlice = 1000;   // render()'s slice: ~0.43 ms of MCU time
    uint64_t renderSilenceFrames() const { return m_renderSilence; }   // frames render() filled with silence (no DSP frame yet)

    // GUI can call (update potentiometers)
    void updatePanel();
    
    // GUI API - Input to emulator
    void sendMIDIData(const uint8_t* data, size_t length);  // Send MIDI from GUI to synth
    void setKnobValue(VR knob, uint16_t value);             // Direct knob setting (for automation)
    void pressButton(MS2000Button button);                  // Press button
    void releaseButton(MS2000Button button);                // Release button
    
    // GUI API - Query emulator state
    uint16_t getKnobValue(VR knob) const;                   // Get current knob position
    bool isButtonPressed(MS2000Button button) const;        // Get button state
    bool isLEDOn(MS2000LED led, LEDColor color) const;      // Get LED state
    std::string getLCDContent() const;                      // Get LCD display content
    
    // fw7.txt Panel-MP access
    PanelMP* getPanelMP() const { return m_panelMP.get(); }
    
    // fw12.txt LCD GUI buffer access for real-time visualization
    LcdGuiSnapshot getLcdGuiSnapshot() const;
    
    // fw14.txt: LCD Boot Probe access for diagnostics
    const LcdBootProbe& getLcdBootProbe() const;
    
    // fw15.txt: Recording and replay functionality
    void startRecording(const std::string& filename);
    void stopRecording();
    bool isRecording() const;
    void startCpuRecording();  // Call after CPU is created

    bool loadReplayFile(const std::string& filename);
    void startReplay();
    void stopReplay();
    bool isReplaying() const;
    
    // fw15.txt: Deterministic state access
    DeterministicState& getDeterministicState() { return m_deterministicState; }
    const DeterministicState& getDeterministicState() const { return m_deterministicState; }
    
    // fw17.txt: Diagnostic panel access
    DiagState& getDiagnosticState() { return m_diagState; }
    const DiagState& getDiagnosticState() const { return m_diagState; }
    
    // fw23.txt: Panel I/O validation access
    PanelIOValidator* getPanelValidator() const { return m_panelValidator.get(); }
    std::vector<PanelTestResult> runPanelValidation();
    void startPanelMonitoring();
    void stopPanelMonitoring();
    
    // fw25.txt: Enhanced LCD validation access
    void initLcdObserver(const std::vector<std::string>& patterns, uint32_t stableMs = 250);
    bool checkLcdPattern();
    bool isLcdStable();

    // LCD display buffer update
    void updateLcdDisplayBuffer();
    void reportLcdTextIfChanged();   // BUG99 instrument, MS2K_LCDTRACE

    // Power and mute state management
    void setPowerState(bool powerOn);
    void setMuteState(bool muted);
    bool isPowerOn() const { return m_powerOn; }
    bool isMuted() const { return m_muted; }
    
    // fw27.txt Phase 3: State snapshot system access
    bool saveSnapshot(const std::string& filename);
    bool loadSnapshot(const std::string& filename);
    bool validateSnapshotRestore(const std::string& filename);
    StateSnapshotManager::ValidationResult compareStates();
    StateSnapshotManager* getSnapshotManager() const { return m_snapshotManager.get(); }
    
    // fw27.txt Phase 4: Long-run soak testing access
    bool startSoakTest(const SoakTestConfig& config);
    void stopSoakTest();
    bool isSoakTestRunning() const;
    SoakTestMonitor::ValidationResult getSoakTestResults() const;
    SoakTestMonitor* getSoakMonitor() const { return m_soakMonitor.get(); }
    
    // fw27.txt Phase 5: Panel timeline & LCD diff visualization
        void enableTimelineVisualization(bool enable);
        std::string getLcdDiffOverlay() const;
        bool exportTimelinePNG(const std::string& filename) const;
        bool exportSwitchSequence(const std::string& filename, uint32_t timeWindowMs = 5000) const;
        PanelTimelineVisualizer::Statistics getVisualizationStats() const;
        PanelTimelineVisualizer* getTimelineVisualizer() const { return m_timelineVisualizer.get(); }

        // Full instrumentation suite - unified access to all instruments
        class InstrumentationManager {
        public:
            explicit InstrumentationManager(Ms2kRunner* runner);
        
            // Enable/disable all instruments at once
            void enableAll();
            void disableAll();
        
            // Individual instrument control
            void setTracing(bool enabled);
            void setDiagPanel(bool enabled);
            void setPanelMonitoring(bool enabled);
            void setTimelineVisualization(bool enabled);
            void setCrashDumps(bool enabled, const std::string& dir = "dumps");
            void setAsyncLogging(bool enabled, size_t queueSize = 4096, uint32_t maxLps = 200);
            void setReplayLogger(bool enabled, const std::string& recordFile = "", const std::string& replayFile = "");
            void setSoakMonitoring(bool enabled, const SoakTestConfig& config = {});
            void setStateSnapshots(bool enabled);
            void setVectorTracking(bool enabled);
            void setSIMDStats(bool enabled);
            void setDeterministic(bool enabled, uint64_t seed = 0);
        
            // Data access
            DiagSnapshot getDiagSnapshot() const;
            void dumpFirstFaultTrace() const;
            void dumpBootLog() const;
            void dumpStackTaintReport() const;
            SoakTestMonitor::ValidationResult getSoakResults() const;
            PanelTimelineVisualizer::Statistics getVisualizationStats() const;
        
            // Export all data
            bool exportAllData(const std::string& baseDir) const;
        
            // Status
            bool isAnyEnabled() const;
            std::string getStatusReport() const;

        private:
            Ms2kRunner* m_runner;
        };

        // Get instrumentation manager
        InstrumentationManager& getInstrumentation() { return *m_instrumentation; }
        const InstrumentationManager& getInstrumentation() const { return *m_instrumentation; }

        // TRAPA→RTE selftest access
        H8S2350Emulator& getEmulator() const { return *m_cpu; }

        // Thin GUI: pace the emulation by the host audio device. While on, the CPU thread runs
        // flat out until the DSP's host-audio ring holds `targetFrames`, then waits for the
        // device to drain it - so the emulated time follows the sound card's clock.
        void setAudioPaced(bool on, uint32_t targetFrames = 2400) { m_audioTarget = targetFrames; m_audioPaced = on; }
        bool isAudioPaced() const { return m_audioPaced; }

    private:
    std::atomic<bool>     m_audioPaced{false};
    std::atomic<uint32_t> m_audioTarget{2400};
    void cpuThreadLoop();
    void audioThreadLoop();
    void postLog(const char* s);
    
    // fw18.txt: MMCSS audio thread management
    void startAudio();
    
    // fw19.txt: I²S/AK4522 audio pipeline management
    void startAudioEngine();
    
    // Boot sequence steps (as per hardware specification)
    void boot_systemResetAssert();
    void boot_mapAndVBR();
    void boot_releaseMcuReset();
    void boot_holdDspReset(bool hold);
    void boot_codecPowerUp();
    void boot_lcdPassive();
    void boot_startPanelScan();
    void boot_wireMailboxAndSSI();
    void boot_startThreads();
    void boot_logReady();
    

    Ms2kConfig    m_cfg;
    Ms2kGuiHooks  m_hooks;

    // subsystems
    std::unique_ptr<MS2000::H8S2350Emulator>   m_cpu;

    std::atomic<bool> m_running{false};
    bool m_manual = false, m_lcdReportPrinted = false;   // ENGINE-BLOCK
    uint64_t m_renderSilence = 0;
    uint64_t m_renderFrames = 0;   // PERF-VST: frames render() has handed out
    int      m_tickPos = 0;        // PERF-VST: steps into the current tick (runSteps)
    std::atomic<bool> m_ready{false};
    std::thread m_cpuThread, m_audioThread;
    
    // fw18.txt: MMCSS audio thread
    AudioThread m_mmcssAudio;
    
    // fw19.txt: I²S/AK4522 audio pipeline
    Ak4522Bridge m_ak4522;
    std::unique_ptr<DspAudioAdapter> m_dspAdapter;
    AudioEngine m_audioEngine;
    
    // LCD state
    char m_lcdBuffer[2][17]; // 2 lines, 16 chars + null terminator
    int m_lcdRow = 0, m_lcdCol = 0;
    // Real LCD and adapter
    std::unique_ptr<RealLCDDisplay> m_realLcd;
    std::unique_ptr<H8SLCDAdapter>  m_lcdAdapter;
    
    // fw7.txt Panel-MP SCI bridge
    std::unique_ptr<PanelMP> m_panelMP;
    
    // GUI state tracking  
    uint16_t m_knobValues[32]; // 32 potentiometers as per hardware spec
    bool m_buttonStates[64] = {false}; // Support up to 64 buttons
    bool m_ledStates[128][3] = {false}; // Support up to 128 LEDs, 3 colors each
    
    // fw15.txt: Deterministic state and recording
    DeterministicState m_deterministicState;
    PanelRecorder m_recorder;
    PanelReplayer m_replayer;
    std::string m_currentRecordingFile;
    bool m_record_mode = false;
    bool m_replay_mode = false;

    // fw17.txt: Diagnostic state tracking
    DiagState m_diagState;
    
    // fw23.txt: Panel I/O validation system
    std::unique_ptr<PanelIOValidator> m_panelValidator;
    
    // fw27.txt Phase 3: State snapshot system
    std::unique_ptr<StateSnapshotManager> m_snapshotManager;
    
    // fw27.txt Phase 4: Long-run soak testing system
    std::unique_ptr<SoakTestMonitor> m_soakMonitor;
    
    // fw27.txt Phase 5: Panel timeline & LCD diff visualization
    std::unique_ptr<PanelTimelineVisualizer> m_timelineVisualizer;
    bool m_timelineEnabled = false;

    // Full instrumentation suite manager
    std::unique_ptr<class InstrumentationManager> m_instrumentation;

    // Power and mute state
    bool m_powerOn = false;
    bool m_muted = true;
};

} // namespace MS2000
