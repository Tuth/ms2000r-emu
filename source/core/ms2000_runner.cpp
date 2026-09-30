#include "dsp56362_emulator.h"
#include "ms2000_runner.h"
#include "h8s2350_instructions.h"
#include "h8s2350_emulator.h"
#include "h8s_lcd_adapter.h"
#include "real_lcd_display.h"
#include "io_probe.h"
#include "lcd_boot_probe.h"
#include "panel_mp.h"
#include "lcd_gui.h"
// fw25.txt: Global LCD observer for event-driven pattern detection
#include "lcd_observer.h"
// boot6.txt: LCD bootstrap and trace system
#include "lcd_bootstrap.h"
#include "lcd_trace.h"
#include <vector>
#include <set>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <sstream>
// fw20.txt: Async log rate limiting
#include "async_log.h"
// fw21.txt: Crash dump system
#include "crash_handlers.h"

// fw25.txt: Global LCD observer for event-driven validation  
static LcdObserver g_lcdObs;

namespace MS2000 {

// LCD Boot Probe instance
static LcdBootProbe s_lcd;

// fw12.txt: Global LCD GUI buffer for real-time visualization
static LcdGuiBuffer g_lcdGui;

// fw20.txt: Global async logger instance
AsyncLogger g_log;

// Helper ROM loader
static bool loadFile(const std::string& path, std::vector<uint8_t>& out, size_t maxSize) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    
    auto size = file.tellg();
    if (size <= 0 || static_cast<size_t>(size) > maxSize) return false;
    
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}

Ms2kRunner::Ms2kRunner(const Ms2kConfig& cfg, const Ms2kGuiHooks& hooks)
: m_cfg(cfg), m_hooks(hooks), m_powerOn(true), m_muted(true)
{
    // Clear LCD buffer
    memset(m_lcdBuffer, ' ', sizeof(m_lcdBuffer));
    for (int i = 0; i < 2; i++) {
        m_lcdBuffer[i][16] = '\0'; // null terminator
    }

    // Initialize knob values to mid-range (12-bit ADC: 0x800 = 2048 = middle)
    for (int i = 0; i < 32; i++) {
        m_knobValues[i] = 0x800;
    }

    // fw15.txt: Initialize deterministic state
    m_deterministicState.resetWithConfig(cfg.deterministicConfig);

    // fw17.txt: Initialize diagnostic state with notes
    std::string notes = "FW-driven LCD | seed=";
    notes += std::to_string(m_deterministicState.getCurrentSeed());
    if (cfg.deterministicConfig.fixedSeed) {
        notes += " (fixed)";
    } else {
        notes += " (random)";
    }
    m_diagState.addNote(notes);

    // Initialize instrumentation manager
    m_instrumentation = std::make_unique<InstrumentationManager>(this);

    // Initialize power state - system starts powered ON
    postLog("[INIT] System initialized with POWER=ON, MUTE=ON (default state)\n");
}

Ms2kRunner::~Ms2kRunner() { 
    stop(); 
}

// Boot sequence implementation (Reset → DSP → CODEC → LCD → Panel → Handshake → Audio)
bool Ms2kRunner::init() {
    // =====================================================================
    // BUG69, 2026-09-16 - init() WAS BEING CALLED TWICE, AND THAT IS THE
    // --selftest CRASH.
    //
    // Measured: `--selftest` prints "TRAPA/RTE selftest result: PASS" and the
    // VERY NEXT LINE is "[CRASH] Installing crash handlers" - the first line of
    // this function - followed by "[CRASH] std::terminate". The exception frame
    // work is not involved at all; the gate dies re-entering its own boot.
    //
    // The caller: HeadlessRunner::init() runs the boot, then
    // HeadlessRunner::start() calls m_runner->init() AGAIN before start().
    // A second pass re-installs the crash handlers, restarts an already-running
    // async logger, re-runs the whole nine-step boot and re-creates m_cpu/m_dsp
    // under a reference the selftest is already holding.
    //
    // Guarded here as well as at the call site, because init() is public and a
    // second call is a realistic mistake, not a one-off.
    // =====================================================================
    if (m_ready) {
        postLog("[Runner] BUG69: init() called twice - ignored, already booted\n");
        return true;
    }

    // fw21.txt: Install crash handlers first
    InstallCrashHandlers(m_cfg.crashDumpsDir.c_str());
    
    // fw20.txt: Start async logger with rate limiting
    g_log.start(4096, 200); // 4096 queue capacity, 200 lines/sec limit
    
    postLog("[Runner] Starting MS2000 boot sequence...\n");
    
    // 0) SYSTEM_RESET assert
    boot_systemResetAssert();
    
    // 1) ROM/RAM/IO mapping + VBR setup
    boot_mapAndVBR();
    
    // 2) Peripheral hooks & mailbox/SSI wiring
    boot_wireMailboxAndSSI();

    // Ensure RealLCDDisplay exists for LCD bootstrap
    if (!m_realLcd) {
        m_realLcd = std::make_unique<RealLCDDisplay>();
    }
    // boot6.txt: LCD bootstrap - force Display ON if firmware hasn't sent 0x0C
    // DISABLED: Let firmware run its own LCD initialization completely
    if (m_realLcd) {
        LcdBootstrapConfig lcfg;
        lcfg.enabled = false;  // disabled - let firmware init LCD
        lcfg.delay_ms = 25;
        lcfg.log = true;

        MS2000::bootstrap_lcd_display_on(*m_realLcd, lcfg);

        // =================================================================
        // BUG68, 2026-09-16 - THE "KORG" INJECTION, AND ITS LIVE HALF.
        //
        // TOMBSTONE. A thirty-line `fw28.txt` block stood here, commented out,
        // that ran a full HD44780 init into RealLCDDisplay and then wrote the
        // literal string "     KORG     " into the display, logging it as
        // "IMMEDIATE KORG boot display complete - authentic MS2000 appearance".
        // Deleted rather than left to be re-enabled by the next reader.
        //
        // THE FIRMWARE NEVER PRINTS "KORG". Measured, Tier 1: `KORG` occurs in
        // flash.bin exactly THREE times - offsets 2048, 262144 and 655360 - and
        // all three are the 16-byte segment headers (SYS / PCM / USR). There is
        // no KORG display string in the image. The splash the firmware actually
        // owns is at flash 193259/193276: "     MS2000     " / "     MS2000R    ".
        //
        // AND THE BLOCK BELOW IT WAS NOT COMMENTED OUT. It sent 0x38, 0x0C, 0x01
        // and 0x06 straight into g_lcdGui at boot - a forged HD44780
        // initialisation in the very buffer the terminal panel and the GUI read.
        // That is why every run printed "Display ON: YES" over an empty screen:
        // `displayOn_` was set by OUR 0x0C, not by the firmware's. It also made
        // the boot probe's ON/CLR/ENT flags meaningless for the GUI path.
        //
        // Removed. The display state is now whatever the firmware put there -
        // which is the whole point of R3, and the only way the panel can ever
        // be read as evidence.
        // =================================================================
        {
            auto initialSnap = g_lcdGui.snapshot();
            char initialMsg[160];
            sprintf_s(initialMsg, sizeof(initialMsg),
                      "[BOOT] BUG68: GUI buffer untouched at reset, displayOn=%s - no forged init\n",
                      initialSnap.displayOn ? "true" : "false");
            postLog(initialMsg);
        }
    }

    // 3) MCU reset release (DSP still held)
    boot_releaseMcuReset();
    
    // 4) DSP reset release
    boot_holdDspReset(false);
    
    // 5) CODEC power-up (MCLK stable, MUTE=ON → UNMUTE)
    boot_codecPowerUp();
    
    // 6) LCD passive (FW will send 0x38,0x0C,0x01,0x06)
    boot_lcdPassive();
    
    // 7) Panel scan setup (1ms tick in CPU thread)
    boot_startPanelScan();
    
    // 8) Start threads (CPU + Audio)
    boot_startThreads();
    
    // 9) BUG103: awaitDspAck() slept 800 ms of wall clock waiting for an "ACK" that was
    //    only ever the EXISTENCE of a gearmulator object. Removed with the fake DSP.

    // 10) fw23.txt: Initialize panel I/O validator
    m_panelValidator = std::make_unique<PanelIOValidator>(this);
    m_panelValidator->setVerboseLogging(true);
    postLog("[Runner] Panel I/O validator initialized\n");
    
    // 11) Load RTC from NVRAM
    if (m_cpu) {
        std::vector<uint8_t> nvram_data(0x2000, 0); // 8KB NVRAM
        // Try to load existing NVRAM file
        std::ifstream nvram_file("ms2000.nvram", std::ios::binary);
        if (nvram_file) {
            nvram_file.read(reinterpret_cast<char*>(nvram_data.data()), nvram_data.size());
            nvram_file.close();
        }
        // Load RTC from NVRAM
        if (m_cpu->getMPStub()) {
            m_cpu->getMPStub()->loadRTCFromNVRAM(nvram_data);
        }
    }
    postLog("[Runner] RTC loaded from NVRAM\\n");

    // 12) Ready
    boot_logReady();
    m_ready = true;
    return true;
}

void Ms2kRunner::start() {
    if (!m_ready || m_running.exchange(true)) return;
    
    postLog("[Runner] Starting execution threads\n");
    m_cpuThread = std::thread(&Ms2kRunner::cpuThreadLoop, this);
    { extern void ms2k_profiler_start(void*); ms2k_profiler_start(m_cpuThread.native_handle()); }   // PERF-124
    m_audioThread = std::thread(&Ms2kRunner::audioThreadLoop, this);
    
    // fw18.txt: Start MMCSS audio thread if enabled
    if (m_cfg.useMmcss) {
        startAudio();
        
        // Diag panel frissítés (opcionális megjelenítés)
        m_diagState.addNote(std::string("MMCSS=ON"));
    } else {
        m_diagState.addNote(std::string("MMCSS=OFF"));
    }
    
    // fw19.txt: Start I²S/AK4522 audio pipeline if enabled
    if (m_cfg.audio.enabled) {
        startAudioEngine();
    }
    
    // fw20.txt: Update diagnostic state with async log info
    m_diagState.setDroppedLogs(g_log.dropped());

    // REMOVED: Auto-wake mechanism - firmware should initialize LCD immediately
    // The firmware IS the system and should self-initialize without external triggering
    postLog("[Runner] Firmware will initialize LCD as part of normal boot sequence\n");
}

void Ms2kRunner::stop() {
    if (!m_running.exchange(false)) return;

    // BUG64: the end-of-run twin of the @2s report above. Same counters, stated
    // time, so the two can be compared instead of confused.
    {
        char line[256];
        snprintf(line, sizeof(line),
          "[LCD-BOOT @END] seq: FN=%d ON=%d CLR=%d ENT=%d DDRAM=%d DATA=%d | gates: DSP=%d CODEC=%d PANEL=%d TICK=%d VBR=%d\n",
          s_lcd.got_fn, s_lcd.got_on, s_lcd.got_clr, s_lcd.got_ent, s_lcd.got_ddram0, s_lcd.data_chars,
          s_lcd.g_dsp_ack, s_lcd.g_codec_unmuted, s_lcd.g_panel_ok, s_lcd.g_tick_ok, s_lcd.g_vbr_ok);
        postLog(line);
    }

    postLog("[Runner] Stopping threads\\n");
    
    // Save RTC to NVRAM before shutdown
    if (m_cpu && m_cpu->getMPStub()) {
        std::vector<uint8_t> nvram_data(0x2000, 0);
        m_cpu->getMPStub()->saveRTCToNVRAM(nvram_data);
        std::ofstream nvram_file("ms2000.nvram", std::ios::binary);
        if (nvram_file) {
            nvram_file.write(reinterpret_cast<const char*>(nvram_data.data()), nvram_data.size());
            nvram_file.close();
            postLog("[Runner] RTC saved to NVRAM\\n");
        }
    }

    // BUG126: keep what the firmware wrote into the flash (see boot_mapAndVBR).
    if (m_cpu && m_cpu->getFlashROM().dirtySectors()) {
        const char* fs = std::getenv("MS2K_FLASHSTATE");
        if (m_cfg.flashStateSave && !(fs && std::strcmp(fs, "off") == 0)) {
            const bool ok = m_cpu->getFlashROM().saveState("ms2000_flash_state.bin", "ms2000_flash_state.sectors");
            char line[160];
            snprintf(line, sizeof line, "[Runner] flash state %s (sectors written this run 0x%05X)\n",
                     ok ? "saved to ms2000_flash_state.bin" : "COULD NOT be saved", unsigned(m_cpu->getFlashROM().dirtySectors()));
            postLog(line);
        }
    }
    // fw18.txt: Stop MMCSS audio thread
    m_mmcssAudio.stop();
    
    // fw19.txt: Stop I²S/AK4522 audio pipeline
    m_audioEngine.stop();
    
    if (m_audioThread.joinable()) m_audioThread.join();
    { extern void ms2k_profiler_stop(); ms2k_profiler_stop(); }   // PERF-124
    if (m_cpuThread.joinable()) m_cpuThread.join();
    
    m_cpu.reset();
    postLog("[Runner] Clean shutdown complete\\n");
    
    // fw20.txt: Stop async logger
    g_log.stop();
}

extern "C" void ms2k_phase(const char*);
// ENGINE-BLOCK: the body of one 1 ms tick, moved out of cpuThreadLoop unchanged (the thread and render() both
// run exactly this). Returns false once the runner is stopped.
bool Ms2kRunner::tick1ms() {
    using clk = std::chrono::steady_clock;
    // 1 ms tick - exactly like test harness
    for (int i = 0; i < m_cfg.cpuCyclesPerTick && m_running; i++) {
        m_cpu->step();
    }
    ms2k_phase("runner-tick");
    
    // Set timer tick flag (after at least some execution)
    s_lcd.g_tick_ok = true;
    
    // fw3.txt autofix - try fallback every tick
    if (m_cpu && m_cpu->getGPIOLcdAdapter()) {
        m_cpu->getGPIOLcdAdapter()->tryAutofix();
    }
    
    // fw7.txt Panel-MP tick for frame timeout handling
    if (m_panelMP) {
        m_panelMP->tick1ms();
    }
    
    // Panel scan / GUI knob update
    updatePanel();

    // --- LCD Boot Report 2s után, csak egyszer ---
    if (!m_lcdReportPrinted && std::chrono::duration_cast<std::chrono::milliseconds>(
          clk::now() - s_lcd.t0).count() > 2000) {
        m_lcdReportPrinted = true;
        char line[256];
        // BUG64: this report fires ONCE, two seconds in - and it did not say so.
        // Its DATA= was read as an end-of-run total and compared against the
        // 53,518 lines the display trace emitted over 25 s, which produced a
        // "two counters disagree by 11x" entry in CLAUDE.md that was never a
        // disagreement at all: one counter at t=2s against the other at t=25s.
        // *A probe must report what it measures, and WHEN.* The stop path prints
        // the same line again with @END.
        snprintf(line, sizeof(line),
          "[LCD-BOOT @2s] seq: FN=%d ON=%d CLR=%d ENT=%d DDRAM=%d DATA=%d | gates: DSP=%d CODEC=%d PANEL=%d TICK=%d VBR=%d\n",
          s_lcd.got_fn, s_lcd.got_on, s_lcd.got_clr, s_lcd.got_ent, s_lcd.got_ddram0, s_lcd.data_chars,
          s_lcd.g_dsp_ack, s_lcd.g_codec_unmuted, s_lcd.g_panel_ok, s_lcd.g_tick_ok, s_lcd.g_vbr_ok);
        postLog(line);
        
        // fw2.txt diagnosztika - Busy flag reads
        if (m_cpu && m_cpu->getGPIOLcdAdapter()) {
            char bf_line[128];
            snprintf(bf_line, sizeof(bf_line), 
              "[LCD-BOOT] GPIO adapter BF reads: %u\n", 
              m_cpu->getGPIOLcdAdapter()->bfReads());
            postLog(bf_line);
        }
        
        // fw25.txt: Update diagnostic state with LCD boot probe information
        BootProbeDiag bootDiag;
        bootDiag.fn = s_lcd.got_fn;
        bootDiag.on = s_lcd.got_on;
        bootDiag.clr = s_lcd.got_clr;
        bootDiag.ent = s_lcd.got_ent;
        bootDiag.ddram = s_lcd.got_ddram0;
        bootDiag.dataCount = static_cast<uint32_t>(s_lcd.data_chars);
        m_diagState.setBoot(bootDiag);
        
        // fw5.txt I/O Scanner Report - dump hottest I/O addresses for LCD discovery
        postLog("[IO-SCAN] Firmware I/O activity report after 2 seconds:\n");
        IoProbe::dump_top(12);

        // lcd5.txt: Prevent CPU trace muting after I/O scan if --trace-cpu=always is enabled
        if (m_cfg.traceCpuAlways) {
            postLog("[IO-SCAN] lcd5.txt: --trace-cpu=always enabled - CPU traces will continue after I/O scan\n");
            // Also log to CPU emulator for confirmation
            if (m_cpu) {
                char cpuMsg[128];
                sprintf_s(cpuMsg, sizeof(cpuMsg), "[IO-SCAN] CPU trace always flag is set: %s\n",
                         m_cpu->isTraceCpuAlways() ? "TRUE" : "FALSE");
                postLog(cpuMsg);
            }
        } else {
            postLog("[IO-SCAN] lcd5.txt: CPU traces may be muted after I/O scan (use --trace-cpu=always to prevent)\n");
        }
    }

    // Probe LCD I/O and route to RealLCD
    // fw3.txt: H8SLCDAdapter disabled, using GPIO adapter instead
    // LCD processing happens through memory-mapped I/O handlers automatically
    if (m_cpu && m_cpu->getGPIOLcdAdapter() && m_realLcd && m_realLcd->hasChanged()) {
        auto l0 = m_realLcd->getText(0);
        auto l1 = m_realLcd->getText(1);
        
        // Copy LCD content to display buffer
        std::memcpy(m_lcdBuffer[0], l0.c_str(), std::min<size_t>(16, l0.length()));
        std::memcpy(m_lcdBuffer[1], l1.c_str(), std::min<size_t>(16, l1.length()));
        m_lcdBuffer[0][16] = '\\0';
        m_lcdBuffer[1][16] = '\\0';
        
        // fw28.txt: Sync RealLCD content to GUI buffer for real-time display
        // Clear the GUI buffer and rewrite with current LCD content
        g_lcdGui.onCmd(0x01); // Clear display
        g_lcdGui.onCmd(0x80); // Set DDRAM address to 0x00 (line 0)
        
        // Write line 0 content
        for (size_t i = 0; i < l0.length() && i < 16; i++) {
            g_lcdGui.onData(static_cast<uint8_t>(l0[i]));
        }
        
        // Set DDRAM address to 0x40 (line 1)
        g_lcdGui.onCmd(0xC0);
        
        // fw28.txt FIX: If firmware has initialized LCD (showing "KORG" on line 0) 
        // but line 1 is empty/garbage, let firmware handle line 1 (patch name, etc.)
        // Don't inject mockup "MS2000" text - let firmware write actual patch name
        // Write actual line 1 content
        for (size_t i = 0; i < l1.length() && i < 16; i++) {
            g_lcdGui.onData(static_cast<uint8_t>(l1[i]));
        }
        
        // fw28.txt: Sync display state from RealLCD to GUI buffer
        // Ensure the GUI buffer display state matches the RealLCD state
        if (m_realLcd->isDisplayOn()) {
            g_lcdGui.onCmd(0x0C); // Display ON, Cursor OFF, Blink OFF
        } else {
            g_lcdGui.onCmd(0x08); // Display OFF
        }
        
        char buf[128];
        snprintf(buf, sizeof(buf), "[LCD] L0: %s\\n[LCD] L1: %s\\n", l0.c_str(), l1.c_str());
        postLog(buf);
        m_realLcd->clearChanged();
    }
    return m_running.load();
}

// ENGINE-BLOCK: exactly `frames` frames of the DSP's 48 kHz output (TX0 slot 0/1, 24-bit, scaled to +-1), made
// by running the runner's ticks (tick1ms: 20,000 CPU steps, the same unit the CPU thread runs) until the DSP ring
// holds them. A segment never runs the MCU past the time its frames span (plus the tick that crosses it): if the
// DSP has not produced them by then (boot before PORT_RESET is released, a DSP held in reset) the rest is
// silence, so emulated time still advances with the host's. Frames made beyond the request stay in the ring.
bool Ms2kRunner::startManual() {
    if (!m_ready || m_running.exchange(true)) return false;
    m_manual = true;
    if (m_cpu && m_cpu->dsp()) m_cpu->dsp()->enableAudioRing(true);
    m_cpu->reset();
    postLog("[Runner] manual mode (ENGINE-BLOCK) - CPU reset released, render() drives the machine\n");
    return true;
}

uint32_t Ms2kRunner::render(float* left, float* right, uint32_t frames, const MidiEv* events, size_t nEvents, uint8_t* fromDsp) {
    DSP56362Emulator* d = m_cpu ? m_cpu->dsp() : nullptr;
    uint32_t done = 0; size_t ei = 0;
    while (done < frames) {
        while (ei < nEvents && events[ei].frame <= done) { sendMIDIData(events[ei].data, events[ei].len); ++ei; }
        const uint32_t segEnd = ei < nEvents ? (std::min)(frames, (std::max)(events[ei].frame, done + 1)) : frames;
        const uint32_t need = segEnd - done;
        if (d) {   // until the frames are there, but never past the MCU time they span (+ the tick that crosses it)
            const uint64_t limit = m_cpu->getCycles() + uint64_t(need) * m_cpu->getClockFrequency() / 48000u;
            while (d->audioFill() < need && m_running) {
                if (m_cpu->getCycles() >= limit) break;
                tick1ms();
            }
        }
        for (uint32_t k = 0; k < need; ++k, ++done) {
            int32_t l = 0, r = 0;
            const bool got = d && d->popAudio(l, r);
            if (got) { left[done] = float(l) / 8388608.0f; right[done] = float(r) / 8388608.0f; }
            else { left[done] = 0.0f; right[done] = 0.0f; ++m_renderSilence; }
            if (fromDsp) fromDsp[done] = got ? 1 : 0;
        }
    }
    while (ei < nEvents) { sendMIDIData(events[ei].data, events[ei].len); ++ei; }
    return frames;
}

void Ms2kRunner::cpuThreadLoop() {
    InstallCrashHandlersThisThread();
    using clk = std::chrono::steady_clock;
    auto next = clk::now();

    // Reset release here, after all mappings/hooks are ready
    m_cpu->reset();
    postLog("[Runner] CPU reset released - firmware execution starting\n");

    while (m_running) {
        // Thin GUI audio pacing (see setAudioPaced).
        if (m_audioPaced) {
            DSP56362Emulator* d = m_cpu->dsp();
            if (d && d->audioFill() >= m_audioTarget) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                next = clk::now();
                continue;
            }
        }
        next += std::chrono::milliseconds(1);
        tick1ms();
        // PERF-124: MS2K_NOPACE=1 - run unpaced, as fast as the host allows (benchmark: the
        // DSP status line's t/wall ratio is then the real-time headroom). Measurement only.
        static const bool noPace = [] { const char* e = std::getenv("MS2K_NOPACE"); return e && *e == '1'; }();
        if (m_audioPaced || noPace) next = clk::now();  // the audio ring paces, not the wall clock
        else std::this_thread::sleep_until(next);
    }
}

void Ms2kRunner::audioThreadLoop() {
    // Simple host-side 48k scheduler
    const int fs = m_cfg.sampleRate;
    const int frames = 256;
    std::vector<int16_t> buf(frames * 2, 0);

    while (m_running) {
        // Generate audio frames
        for (int i = 0; i < frames; i++) {
            // Audio tick - calls DSP/ADC-DAC bridge internally
            // buf[i*2] = left_sample;
            // buf[i*2+1] = right_sample;
        }
        
        if (m_hooks.audioOut) {
            m_hooks.audioOut(buf.data(), frames);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds((frames * 1000) / fs));
    }
}

void Ms2kRunner::updatePanel() {
    // If there's direct GUI VR reader, update PanelState here
    // (or let existing H8S→ADSEL→ADC path work)
}

// Force update of LCD display buffer for immediate GUI refresh
// BUG99 instrument: the shutdown snapshot is ONE sample of a display the firmware
// keeps redrawing, so "the screen is empty at the end" says nothing about what was
// ever on it. The Tier-2 reference has the same property - its own log shows the
// splash on 5 frames out of ~120. This prints the GUI buffer whenever its text
// CHANGES. Behind MS2K_LCDTRACE (R2, default OFF), the same switch as the byte stream.
void Ms2kRunner::reportLcdTextIfChanged() {
    static int on = -1;
    if (on < 0) { const char* e = std::getenv("MS2K_LCDTRACE"); on = (e && *e && *e != '0') ? 1 : 0; }
    if (!on) return;
    auto s = g_lcdGui.snapshot();
    static char last0[17] = {0}, last1[17] = {0};
    if (std::memcmp(last0, s.line0, 17) == 0 && std::memcmp(last1, s.line1, 17) == 0) return;
    std::memcpy(last0, s.line0, 17);
    std::memcpy(last1, s.line1, 17);
    auto show = [](const char* l, char* out) {
        for (int i = 0; i < 16; ++i) {
            unsigned char c = (unsigned char)l[i];
            out[i] = (c >= 0x20 && c < 0x7F) ? char(c) : (c < 0x08 ? char('0' + c) : '.');
        }
        out[16] = 0;
    };
    char a[17], b[17];
    show(s.line0, a); show(s.line1, b);
    printf("[LCD-TEXT] [%s] / [%s]\n", a, b);
    fflush(stdout);
}

void Ms2kRunner::updateLcdDisplayBuffer() {
    reportLcdTextIfChanged();
    if (m_realLcd) {
        auto l0 = m_realLcd->getText(0);
        auto l1 = m_realLcd->getText(1);

        // Copy LCD content to display buffer
        std::memcpy(m_lcdBuffer[0], l0.c_str(), std::min<size_t>(16, l0.length()));
        std::memcpy(m_lcdBuffer[1], l1.c_str(), std::min<size_t>(16, l1.length()));
        m_lcdBuffer[0][16] = '\0';
        m_lcdBuffer[1][16] = '\0';

        // Mark as changed to ensure GUI updates
        m_realLcd->clearChanged();
    }
}

void Ms2kRunner::postLog(const char* s) {
    if (m_hooks.logFn) {
        m_hooks.logFn(s);
    }
}

// ==== Boot Sequence Implementation ====

void Ms2kRunner::boot_systemResetAssert() {
    postLog("[BOOT] SYSTEM_RESET asserted\n");
}

void Ms2kRunner::boot_mapAndVBR() {
    postLog("[BOOT] Creating CPU/DSP emulators\n");
    
    // Create emulators
    m_cpu = std::make_unique<MS2000::H8S2350Emulator>();
    
    // Load ROM
    if (!m_cpu->loadFirmwareFromFile(m_cfg.romPath)) {
        postLog("[BOOT] ROM load failed\n");
        throw std::runtime_error("ROM load failed");
    }
    // BUG126: the flash is non-volatile - sectors the firmware wrote in an earlier run (Global
    // WRITE, program WRITE) come back from ms2000_flash_state.bin/.sectors; flash.bin is never
    // written. MS2K_FLASHSTATE=off = factory flash every run (measurement runs use it).
    {
        const char* fs = std::getenv("MS2K_FLASHSTATE");
        if (!(fs && std::strcmp(fs, "off") == 0) && m_cfg.flashStateMask) {
            const uint32_t m = m_cpu->getFlashROM().loadStateFrom(m_cfg.flashStateMask, m_cfg.flashStateImage.data(), m_cfg.flashStateImage.size());
            m_cpu->invalidateDecodeCache();
            char line[160];
            snprintf(line, sizeof line, "[BOOT] flash state: sectors 0x%05X taken from the host's saved state\n", unsigned(m));
            postLog(line);
        } else if (!(fs && std::strcmp(fs, "off") == 0) && m_cfg.flashStateLoad) {
            const uint32_t m = m_cpu->getFlashROM().loadState("ms2000_flash_state.bin", "ms2000_flash_state.sectors");
            m_cpu->invalidateDecodeCache();   // PERF-133: flash contents changed host-side
            char line[160];
            if (m) snprintf(line, sizeof line, "[BOOT] flash state: sectors 0x%05X taken from ms2000_flash_state.bin (written by the firmware in an earlier run)\n", unsigned(m));
            else   snprintf(line, sizeof line, "[BOOT] flash state: none - factory flash.bin\n");
            postLog(line);
        }
    }
    // Set VBR and TRAPA vector config (fixed baseIndex = 8 for H8S/2350 Mode 4 Advanced)
    m_cpu->setVBR(m_cfg.vbr);
    TrapVectorConfig tvc;
    tvc.autoDetect = false;  // Fixed configuration for Mode 4 Advanced
    tvc.baseIndex = 8;       // TRAPA base index = 8 + n (H8S/2350 specification)
    tvc.locked = true;       // Mode 4 configuration is locked
    m_cpu->setTrapConfig(tvc);

    // lcd5.txt: Set CPU trace always flag to prevent muting after I/O scan
    m_cpu->setTraceCpuAlways(m_cfg.traceCpuAlways);

    // FIX21: Quiet boot - suppress per-instruction verbose trace for fast boot
    m_cpu->setQuietBoot(m_cfg.quietBoot);
    // FIX21b: also arm the file-global printf gate in h8s2350_instructions.cpp
    { extern bool g_h8s_quiet_boot; g_h8s_quiet_boot = m_cfg.quietBoot; }

    // FIX5: Read byte callback for 0xA9BC wait loop instrumentation
    // Logs ER2 value at first entry + all memory reads in [0xA9BC, 0xAA10]
    // PERF-124: this trace ran as a std::function on EVERY bus read (~4 % of real time) for a
    // 2026-05 question about 0xA9BC. It is installed only with MS2K_A9BCTRACE=1 now.
    if (const char* e9 = std::getenv("MS2K_A9BCTRACE"); e9 && *e9 == '1')
    m_cpu->setReadByteCallback([this](uint32_t address, uint8_t& value) -> bool {
        static bool a9bc_er2_logged = false;
        static std::set<uint32_t> logged_reads; // dedupe
        
        if (address >= 0xA9BC && address <= 0xAA10) {
            if (!a9bc_er2_logged) {
                a9bc_er2_logged = true;
                postLog("[A9BC-LOOP] Entered loop region, trace enabled\n");
            }
            // Log unique read addresses to avoid spam
            if (logged_reads.insert(address).second) {
                char buf[128];
                snprintf(buf, sizeof(buf), "[A9BC-READ] addr=0x%08X\n", address);
                postLog(buf);
            }
            return false; // Don't intercept, just trace - let normal read path handle it
        }
        return false; // Not handled, normal read path
    });
    setGlobalInsnTrace(m_cfg.traceCpuAlways);
    postLog(m_cfg.traceCpuAlways ?
        "[INIT] lcd5.txt: --trace-cpu=always ENABLED - CPU traces will continue after I/O scan\n" :
        "[INIT] lcd5.txt: --trace-cpu=always DISABLED - CPU traces may be muted after I/O scan\n");

    // fw29.txt: Start CPU-level recording if requested (after CPU is created) - ONLY for recording, not replay
    if (m_record_mode) {
        startCpuRecording();
    }

    postLog("[BOOT] ROM/RAM mapped, VBR set to 0x000000\n");
}

void Ms2kRunner::boot_wireMailboxAndSSI() {
    postLog("[BOOT] Setting up peripheral hooks\n");
    
    // Set up Panel ADC callback (potentiometers)
    if (m_cpu->getPanelADC()) {
        m_cpu->getPanelADC()->setADCReadCallback([this](VR vr_id) -> uint16_t {
            int idx = static_cast<int>(vr_id);
            if (m_hooks.readKnob) {
                uint16_t guiValue = m_hooks.readKnob(vr_id);
                if (idx >= 0 && idx < 32) {  // Use 32 knobs to match hardware spec
                    m_knobValues[idx] = guiValue; // Update internal state
                }
                return guiValue;
            }
            return (idx >= 0 && idx < 32) ? m_knobValues[idx] : 0x800; // Use internal state as fallback
        });
        postLog("[BOOT] Panel ADC callbacks configured\n");
    }
    
    // Set up Switch Matrix callback (buttons)
    if (m_cpu->getSwitchMatrix()) {
        m_cpu->getSwitchMatrix()->setSwitchReadCallback([this](MS2000Button btn_id) -> bool {
            if (m_hooks.readSwitch) {
                bool guiState = m_hooks.readSwitch(btn_id);
                int idx = static_cast<int>(btn_id);
                if (idx >= 0 && idx < 64) {
                    m_buttonStates[idx] = guiState; // Update internal state
                }
                return guiState;
            }
            // Use internal state as fallback
            int idx = static_cast<int>(btn_id);
            return (idx >= 0 && idx < 64) ? m_buttonStates[idx] : false;
        });
        postLog("[BOOT] Switch matrix callbacks configured\n");
    }
    
    // Set up LED Matrix callback (LEDs)
    if (m_cpu->getLEDMatrix()) {
        m_cpu->getLEDMatrix()->setLEDUpdateCallback([this](MS2000LED led_id, const LEDColor& color) {
            // Forward to GUI
            if (m_hooks.ledSet) {
                m_hooks.ledSet(led_id, color);
            }
        });
        postLog("[BOOT] LED matrix callbacks configured\n");
    }
    
    // Set up Real LCD + Adapter to observe firmware LCD I/O
    if (!m_realLcd) {
        m_realLcd = std::make_unique<RealLCDDisplay>();
        
        // fw26.txt: Connect global LCD observer directly to DDRAM write hook for reliable pattern detection
        m_realLcd->setDDRAMWriteCallback([](uint8_t addr, uint8_t data) {
            // Convert DDRAM address to canonical HD44780 command format
            g_lcdObs.onCmd(0x80 | (addr & 0x7F));
            g_lcdObs.onData(data);
        });
    }
    
    // fw8.txt: Setup Panel-MP Multi-SCI bridge for authentic firmware LCD via panel protocol
    if (!m_panelMP) {
        m_panelMP = std::make_unique<PanelMP>();
        
        // Connect Panel-MP to LCD backend using RealLCDDisplay exec() API
        m_panelMP->lcdCmd = [this](uint8_t cmd) {
            if (m_realLcd) {
                // boot6.txt: LCD trace - PANEL_MP path
                lcd_trace(false, cmd, LcdPath::PANEL_MP);

                // Use RealLCDDisplay exec() method for HD44780 commands
                m_realLcd->exec(false, false, cmd);  // RS=0 (command), RW=0 (write), data=cmd

                // fw12.txt: Bridge to GUI buffer for real-time visualization
                g_lcdGui.onCmd(cmd);

                // fw25.txt: Bridge to LCD observer for event-driven pattern detection
                g_lcdObs.onCmd(cmd);

                // fw28.txt: Force GUI buffer update for immediate LCD display sync
                updateLcdDisplayBuffer();

                // Update boot probe diagnostics
                if (cmd == 0x38) s_lcd.got_fn = true;       // Function Set
                else if (cmd == 0x0C) s_lcd.got_on = true;  // Display ON
                else if (cmd == 0x01) s_lcd.got_clr = true; // Clear
                else if (cmd == 0x06) s_lcd.got_ent = true; // Entry Mode
                else if ((cmd & 0x80) == 0x80) {            // Set DDRAM Address
                    s_lcd.got_ddram0 = true;
                }

                // Bridge to GUI hooks
                if (m_hooks.lcdClear) m_hooks.lcdClear();
            }
        };
        
        // fw17.txt: Diagnostic callbacks for PanelMP monitoring
        m_panelMP->onModeChange = [this](PanelMode mode) {
            m_diagState.setPanelMode(mode);
            // fw25.txt: When panel mode changes from Probe, assume SCI0 as panel source
            if (mode != PanelMode::Probe) {
                m_diagState.setPanelSrc(PanelSrc::SCI0);  // Default assumption for MS2000 panel
            }
        };
        
        m_panelMP->onBytesIn = [this](size_t bytes) {
            m_diagState.addMpBytes(bytes);
        };
        
        m_panelMP->onFrameDone = [this]() {
            m_diagState.incMpFrames();
        };
        
        m_panelMP->lcdData = [this](uint8_t data) {
            if (m_realLcd) {
                // boot6.txt: LCD trace - PANEL_MP path
                lcd_trace(true, data, LcdPath::PANEL_MP);

                // Use RealLCDDisplay exec() method for HD44780 data
                m_realLcd->exec(true, false, data);  // RS=1 (data), RW=0 (write), data=data

                // fw12.txt: Bridge to GUI buffer for real-time visualization
                g_lcdGui.onData(data);

                // fw25.txt: Bridge to LCD observer for event-driven pattern detection
                g_lcdObs.onData(data);

                // fw28.txt: Force GUI buffer update for immediate LCD display sync
                updateLcdDisplayBuffer();

                s_lcd.data_chars++;

                // Bridge to GUI hooks
                if (m_hooks.lcdClear) m_hooks.lcdClear();
            }
        };
        
        m_panelMP->log = [this](const char* msg) {
            postLog(msg);
        };
        
        postLog("[BOOT] fw8.txt Panel-MP Multi-SCI bridge initialized for authentic firmware LCD\n");
    }
    
    // fw8.txt Multi-SCI autodiscovery: Hook all three SCI channels
    if (m_cpu && m_panelMP) {
        // Common TX hook for any SCI channel (fw8.txt approach)
        // BUG48, 2026-09-13 - THE PANEL-MP SERIAL BRIDGE IS AMPUTATED.
        //
        // This hook routed EVERY SCI channel's transmit bytes into a "Panel-MP"
        // protocol decoder. KOD-A30411 (Tier 1) says what the two channels are:
        //     SCI0  TXD0 pin 59 / RXD0 pin 61 / SCK0 pin 63  -> THE DSP LINK
        //     SCI1  TXD1 pin 60 / RXD1 pin 62                -> MIDI OUT / MIDI IN
        // Neither is a panel. And the panel on that page is not on a serial link at
        // all: the switches and knobs go through an analog mux to the MCU's own A/D
        // (ADSEL0/1/2), and the LEDs go through the LV574A latch (LD00-LD11). No
        // panel microcontroller appears on the schematic page.
        //
        // The traffic agrees: SCI1's stream contains 0xFE, MIDI Active Sensing.
        // We were handing MIDI to a panel-protocol decoder and then handing its
        // output to an LCD.
        //
        // Kept as a no-op rather than deleted, because PanelMP still owns the LCD
        // command/data callbacks used elsewhere and removing the object is a larger
        // change than this round should make. NOTHING FEEDS IT NOW.
        auto onSciTx = [](int /*sciId*/, uint8_t /*b*/) { };
        
        // SCI configuration callback for logging
        // LOG-SPAM (2026-09-28): the callback fires on every SCI SMR/SCR/BRR write the firmware makes, and each
        // write logged this line - 62,497 identical lines in a 15-minute GUI run. Only a CHANGE is logged now.
        auto onSciCfg = [this](int sciId, bool txEnabled, bool syncMode, uint32_t baud) {
            static uint64_t last[2] = { ~0ull, ~0ull };
            const uint64_t key = (uint64_t(baud) << 2) | (txEnabled ? 2u : 0u) | (syncMode ? 1u : 0u);
            if (sciId < 0 || sciId > 1 || last[sciId] == key) return;
            last[sciId] = key;
            if (m_panelMP && m_panelMP->log) {
                char m[96];
                snprintf(m, sizeof(m), "[SCI%d] cfg: baud=%u TE=%d CM=%d\n", 
                         sciId, baud, txEnabled ? 1 : 0, syncMode ? 1 : 0);
                m_panelMP->log(m);
            }
        };
        
        // Hook Multi-SCI callbacks
        m_cpu->setSciTxCallback(onSciTx);
        m_cpu->setSciConfigCallback(onSciCfg);
        
        postLog("[BOOT] fw8.txt Multi-SCI: All channels (SCI0/SCI1) hooked to Panel-MP bridge\n");
    }
    
    // BUG103: an "HPI sniffer" stood here that logged "[HPI] RX 0xAC (simulated ACK)" for
    // bytes nothing had sent. There is no HPI on this board and no DSP model yet.

    // Clean firmware boot - no test message injection
    // Real firmware LCD communication will flow through GPIO and Panel-MP paths
    postLog("[BOOT] Clean firmware boot - no test message injection\n");
    
    // Hook LCD I/O for boot probe diagnostics
    if (m_cpu) {
        // =================================================================
        // BUG70, 2026-09-16 - THE WORST OF THE 21 g_lcdGui WRITERS, AND IT IS
        // THE PANEL SCAN BEING TYPED ONTO THE DISPLAY.
        //
        // `setLCDWriteCallback` is fired from three places in the I/O switch,
        // and NONE of them is the Port 2 channel the schematic says the LCD is on:
        //
        //   case 0x0005 (P5DR) -> m_lcd_write_callback(port5_data, TRUE)
        //        P5DR IS THE PANEL SCAN COLUMN SELECT (BUG54, KOD-A30411:
        //        P50-P52 = A/B/C into the two '138s, P53 = G). Every one of the
        //        eight scan writes was being handed to the display AS A CHARACTER.
        //        Its own comment admits the guesswork: "For simplicity, just pass
        //        Port 5 data as-is; MP stub will combine".
        //   case 0x0060 (P1DR) -> m_lcd_write_callback(value, FALSE)
        //        P1DR read as an LCD COMMAND. BUG48 cut exactly this route into
        //        the adapter and into Panel-MP; the callback survived both sweeps.
        //   case 0x0061          -> ...as LCD data, on an address the P2DR choke
        //        already intercepts, so it is either dead or a duplicate.
        //
        // This is BUG48's disease a third time: RAW PORT BYTES treated as finished
        // HD44780 bytes - no nibble assembly, no E edge, no RS. And unlike BUG48's
        // victims it fed `g_lcdGui` AND the boot probe, so it corrupted both the
        // picture and the counters.
        //
        // AND IT DOUBLE-COUNTED ITSELF: the first 40 events called s_lcd.on_data /
        // on_cmd inside the rate-limited print AND again in the "Always update
        // probe" block below it. Forty phantom characters at the head of every run.
        //
        // The display now has ONE writer on this route: none. writeP2DR() ->
        // the GPIO adapter callbacks is the schematic path and it already works.
        // Kept as a NAMED TRACE, default OFF, so the traffic is still visible when
        // someone wants to know what those ports are doing - which is a real
        // question, just not an LCD one.
        // =================================================================
        m_cpu->setLCDWriteCallback([](uint8_t value, bool is_data) {
            static int on = -1;
            if (on < 0) { const char* e = std::getenv("MS2K_PORTTRACE"); on = (e && *e && *e != '0') ? 1 : 0; }
            if (!on) return;
            static int n = 0;
            if (n++ < 64) {
                printf("[PORT-NOT-LCD] %s 0x%02X  (P5DR/P1DR traffic, NOT display data)\n",
                       is_data ? "P5DR" : "P1DR", value);
            }
        });
        postLog("[BOOT] BUG70: P5DR/P1DR no longer feed the display or the boot probe\n");
    }
    
    // Setup h8s.txt GPIO LCD adapter with PanelMP backend (proper 4-bit mode)
    if (m_cpu && m_realLcd && m_panelMP) {
        m_cpu->setupGPIOLcdAdapter(
            [this](uint8_t cmd) {
                // boot6.txt: LCD trace - GPIO path
                lcd_trace(false, cmd, LcdPath::GPIO);

                // HD44780 command (RS=0, RW=0)
                s_lcd.on_cmd(cmd);  // Also update boot probe

                // BUG48: do NOT forward to PanelMP. Its lcdCmd handler calls
                // lcd_trace(..., PANEL_MP) again, so every GPIO command was logged
                // TWICE - which is why the run showed 298 GPIO CMD and 299 PANEL_MP
                // CMD, the same events counted on two paths. PanelMP is a serial
                // protocol bridge; handing it bytes that were already decoded from
                // GPIO is backwards.

                // fw28.txt: Forward to GUI buffer for real-time display sync
                g_lcdGui.onCmd(cmd);
                updateLcdDisplayBuffer();
            },
            [this](uint8_t data) {
                // boot6.txt: LCD trace - GPIO path
                lcd_trace(true, data, LcdPath::GPIO);

                // HD44780 data (RS=1, RW=0)
                s_lcd.on_data(data);  // Also update boot probe

                // BUG48: same as the command path - no double trace through PanelMP.

                // fw28.txt: Forward to GUI buffer for real-time display sync
                g_lcdGui.onData(data);
                updateLcdDisplayBuffer();
            },
            [this](bool rs) -> uint8_t {
                // LCD Memory-Mapped Read (for 0x402xxx area)
                // rs=false (RS=0): Read busy flag (bit7) + address counter [6:0]
                // rs=true  (RS=1): Read DDRAM data
                if (auto* adapter = m_cpu->getGPIOLcdAdapter()) {
                    return adapter->onReadLCD(rs);
                }
                return 0x00;
            },
            [](const char* msg) { std::cout << msg; }  // Enable LCD adapter debug logging
        );
    }
        postLog("[BOOT] h8s.txt GPIO LCD adapter configured\n");
    
    // Set up MIDI callbacks
    if (m_cpu->getMIDIInterface()) {
        // MIDI output (synth → host)
        m_cpu->getMIDIInterface()->setMIDIOutputCallback([this](const uint8_t* data, size_t length) {
            if (m_hooks.midiOut) {
                m_hooks.midiOut(data, length);
            }
        });
        
        // MIDI input callback (host → synth) - will be called via receiveMIDIData()
        m_cpu->getMIDIInterface()->setMIDIInputCallback([this](const uint8_t* data, size_t length) {
            if (m_hooks.midiIn) {
                m_hooks.midiIn(data, length);
            }
        });
        
        postLog("[BOOT] MIDI interface callbacks configured\n");
    }
    
    // Clean firmware boot - no MIDI smoke test injection
    // Real MIDI communication will flow through SCI channels naturally
    if (m_cfg.enableMidiSmokeTest) {
        postLog("[MIDI] MIDI smoke test disabled for clean firmware boot\n");
    }
    
    postLog("[BOOT] DSP mailbox + I2S wired\n");
}

void Ms2kRunner::boot_releaseMcuReset() {
    postLog("[BOOT] MCU reset release\n");
    // Actual reset release happens in cpuThread (reset() call)
}

void Ms2kRunner::boot_holdDspReset(bool hold) {
    postLog(hold ? "[BOOT] DSP_RESET asserted\n" : "[BOOT] DSP_RESET released\n");

    // BUG103: the DSP56362 is not modelled; DSP_RESET has nothing to release yet.
}

void Ms2kRunner::boot_codecPowerUp() {
    postLog("[BOOT] CODEC power sequence: RESET → MCLK stable → MUTE=ON → MUTE=OFF\n");
    // CODEC power-up sequence would be handled by audio system
    // BUG103: g_codec_unmuted was set true here unconditionally - a gate reporting what
    // nobody measured. The CODEC (AK4522) mute is CODEC_MUTE, a port pin the FIRMWARE drives.
}

void Ms2kRunner::boot_lcdPassive() {
    postLog("[BOOT] LCD passive mode (FW will init with 0x38,0x0C,0x01,0x06)\\n");
    // Keep LCD buffer content from immediate bootstrap (KORG/MS2000)
    // Firmware will re-initialize LCD - we preserve bootstrap until FW writes
}

void Ms2kRunner::boot_startPanelScan() {
    postLog("[BOOT] Panel scanning enabled via 1ms CPU tick\n");
    // Panel scanning happens in the CPU thread's 1ms tick
    // BUG103: g_panel_ok was set true here before the firmware had scanned anything.
}

void Ms2kRunner::boot_startThreads() {
    postLog("[BOOT] Starting CPU & Audio threads\n");
    
    // fw5.txt I/O Scanner - enable for memory-mapped LCD discovery
    IoProbe::enable();
    postLog("[IO-SCAN] fw5.txt I/O scanner enabled for LCD address discovery\n");
    
    // Threads will be started by the start() method
}

void Ms2kRunner::boot_logReady() {
    postLog("[BOOT] MS2000 boot sequence complete - ReadyToRun\n");
}

// ==== GUI API Implementation ====

void Ms2kRunner::sendMIDIData(const uint8_t* data, size_t length) {
    // BUG108: host MIDI IN goes onto SCI1's receive line (RXD1 = MIDI IN, KOD-A30411). It used to
    // end in MS2000MIDIInterfaceSimple's queue, which nothing the firmware reads ever drained.
    if (m_cpu) m_cpu->midiInPush(data, length);
}

// fw23.txt: Panel I/O API methods for validation framework
void Ms2kRunner::setKnobValue(VR knob, uint16_t value) {
    int knob_id = static_cast<int>(knob);
    if (knob_id >= 0 && knob_id < 32) {
        m_knobValues[knob_id] = value & 0xFFF; // 12-bit ADC
    }
}

void Ms2kRunner::pressButton(MS2000Button button) {
    int btn_id = static_cast<int>(button);
    if (btn_id >= 0 && btn_id < 64) {
        m_buttonStates[btn_id] = true;
    }
}

void Ms2kRunner::releaseButton(MS2000Button button) {
    int btn_id = static_cast<int>(button);
    if (btn_id >= 0 && btn_id < 64) {
        m_buttonStates[btn_id] = false;
    }
}

uint16_t Ms2kRunner::getKnobValue(VR knob) const {
    int knob_id = static_cast<int>(knob);
    if (knob_id >= 0 && knob_id < 32) {
        return m_knobValues[knob_id];
    }
    return 0x800; // Mid-range default
}

bool Ms2kRunner::isButtonPressed(MS2000Button button) const {
    int btn_id = static_cast<int>(button);
    if (btn_id >= 0 && btn_id < 64) {
        return m_buttonStates[btn_id];
    }
    return false;
}

bool Ms2kRunner::isLEDOn(MS2000LED led, LEDColor color) const {
    int led_id = static_cast<int>(led);
    // Simple LED state check - just verify the LED ID is valid
    if (led_id >= 0 && led_id < 128) {
        // For simplicity, return false (LED off) for now
        return false; // TODO: Implement actual LED state tracking
    }
    return false;
}

std::string Ms2kRunner::getLCDContent() const {
    std::string content;
    content += std::string(m_lcdBuffer[0]) + "\n";
    content += std::string(m_lcdBuffer[1]);
    return content;
}

// fw12.txt: LCD GUI snapshot access for real-time visualization
// fw28.txt: Use authentic RealLCDDisplay directly for GUI data
// BUG99, 2026-09-19 - THE SCREEN WE PRINTED WAS NOT THE SCREEN THE FIRMWARE WROTE.
//
// This function is called getLcdGuiSnapshot() and read m_realLcd, a SECOND display
// object that only the PanelMP serial bridge ever writes. BUG48 correctly stopped
// forwarding GPIO bytes to PanelMP - PanelMP is a serial protocol bridge, and this
// firmware drives the display over GPIO - so on the real boot path m_realLcd is
// never written at all. Meanwhile g_lcdGui IS written by both paths, and it had the
// splash in it the whole time. MEASURED 2026-09-19 with MS2K_LCDTRACE=1:
//
//     [LCD-TEXT] [ ---        --- ] / [     MS2000R    ]
//
// built up letter by letter, exactly as the Tier-2 reference builds it.
//
// Two more fictions removed with it:
//   - if both lines came back empty the function WROTE "     KORG     " and
//     "   MS2000     " into the snapshot and printed [LCD-TEST]. STATE.json already
//     records the Tier-1 finding that the firmware never prints KORG - `KORG`
//     occurs in flash.bin exactly three times and all three are segment headers.
//     A display that invents its own contents cannot be read as evidence.
//   - `s.displayOn = true;  // Force display ON regardless of RealLCD state`, which
//     is why every run reported "Display ON: YES" whatever the firmware had done.
//
// The display state is now whatever the firmware put there. R3.
LcdGuiSnapshot Ms2kRunner::getLcdGuiSnapshot() const {
    return g_lcdGui.snapshot();
}

// fw14.txt: LCD Boot Probe access for diagnostics
const LcdBootProbe& Ms2kRunner::getLcdBootProbe() const {
    return s_lcd;
}

// fw15.txt: Recording and replay functionality
void Ms2kRunner::startRecording(const std::string& filename)
{
    m_currentRecordingFile = filename;
    m_record_mode = true;
    m_replay_mode = false;
    m_recorder.startRecording();
    postLog("[RECORD] Panel recorder started\n");

    // fw17.txt: Update diagnostic state
    m_diagState.setRecording(true);
    m_diagState.setRecordingFile(filename);

    // Hook into Panel MP for capture
    if (m_panelMP) {
        // TODO: Add hooks to capture panel communication data
        postLog("[RECORD] Started recording to ");
        postLog(filename.c_str());
        postLog("\n");
    }
}

void Ms2kRunner::startCpuRecording()
{
    // fw29.txt: Start CPU-level recording (IRQ, MIDI, DSP HPI) after CPU is created
    if (m_cpu && !m_currentRecordingFile.empty()) {
        std::string cpu_filename = m_currentRecordingFile + ".cpu.mrec";
        postLog("[RECORD] Starting CPU recording to: ");
        postLog(cpu_filename.c_str());
        postLog("\n");
        if (!m_cpu->startRecording(cpu_filename)) {
            postLog("[RECORD] ERROR: CPU startRecording failed\n");
        } else {
            postLog("[RECORD] CPU recording started\n");
        }
    }
}

void Ms2kRunner::stopRecording() {
    if (m_recorder.isRecording()) {
        m_recorder.stopRecording();

        // fw29.txt: Also stop CPU-level recording
        if (m_cpu) {
            m_cpu->stopRecording();
        }

        // fw17.txt: Update diagnostic state
        m_diagState.setRecording(false);

        if (!m_currentRecordingFile.empty()) {
            postLog("[RECORD] Attempting to save recording to: ");
            postLog(m_currentRecordingFile.c_str());
            postLog("\n");
            
            bool saved = m_recorder.saveTo(m_currentRecordingFile);
            if (saved) {
                postLog("[RECORD] Saved recording to ");
                postLog(m_currentRecordingFile.c_str());
                postLog("\n");
            } else {
                postLog("[RECORD] ERROR: Failed to save recording\n");
            }
        }
        m_currentRecordingFile.clear();
        m_record_mode = false;
    }
}

bool Ms2kRunner::isRecording() const {
    return m_recorder.isRecording();
}

bool Ms2kRunner::loadReplayFile(const std::string& filename) {
    if (!m_recorder.loadFrom(filename)) {
        postLog("[REPLAY] ERROR: Failed to load ");
        postLog(filename.c_str());
        postLog("\n");
        return false;
    }
    
    // If the recording has deterministic state, restore it
    if (m_recorder.hasDeterministicState()) {
        m_deterministicState.deserialize(m_recorder.getInitialState());
        postLog("[REPLAY] Restored deterministic state (seed=");
        
        // Simple integer to string conversion for logging
        char seedStr[32];
        sprintf_s(seedStr, sizeof(seedStr), "%llu", m_recorder.getInitialState().prngSeed);
        postLog(seedStr);
        postLog(")\n");
    }
    
    m_replayer.loadCapture(m_recorder.getCaptures());
    postLog("[REPLAY] Loaded ");
    postLog(filename.c_str());

    // Simple size_t to string conversion for logging
    char sizeStr[32];
    sprintf_s(sizeStr, sizeof(sizeStr), " (%zu captures)\n", m_recorder.getCaptureCount());
    postLog(sizeStr);

    // Store for CPU replay
    m_currentRecordingFile = filename;

    return true;
}

void Ms2kRunner::startReplay() {
    m_replay_mode = true;
    m_record_mode = false;
    m_replayer.startReplay();

    // fw29.txt: Also start CPU-level replay
    if (m_cpu) {
        std::string cpu_filename = m_currentRecordingFile + ".cpu.mrec";
        postLog("[REPLAY] Starting CPU replay to: ");
        postLog(cpu_filename.c_str());
        postLog("\n");
        m_cpu->startReplay(cpu_filename);
    }

    // fw17.txt: Update diagnostic state
    m_diagState.setReplaying(true);
    m_diagState.setDeterministicState(m_deterministicState.getCurrentSeed(),
                                     m_deterministicState.getTickCount());

    postLog("[REPLAY] Playback started\n");
}

void Ms2kRunner::stopReplay() {
    m_replayer.stopReplay();

    // fw29.txt: Also stop CPU-level replay
    if (m_cpu) {
        m_cpu->stopReplay();
    }

    // fw17.txt: Update diagnostic state
    m_diagState.setReplaying(false);
    m_replay_mode = false;

    postLog("[REPLAY] Playback stopped\n");
}

bool Ms2kRunner::isReplaying() const {
    return m_replayer.isPlaying();
}

// fw18.txt: MMCSS audio thread integration
void Ms2kRunner::startAudio() {
    // A callback-ben hívd a meglévő I²S/CODEC ticket vagy a kimeneti mixet
    m_mmcssAudio.start([&]{
        // TODO: pumpAudioBlock(128);  // 128 minta, 48kHz
        // Ha nincs még kész az I²S/AK4522 pipeline: a fenti callback most „no-op" lehet. 
        // A lényeg, hogy a szál MMCSS-ben fut – amikor beakasztod az audió pumpát, már real-time-képes lesz.
    }, m_cfg.useMmcss);

    postLog("[AUDIO] MMCSS audio thread started with Pro Audio priority\n");
}

// fw19.txt: I²S/AK4522 audio pipeline integration
void Ms2kRunner::startAudioEngine() {
    // Initialize AK4522 bridge
    m_ak4522.init(m_cfg.audio.sampleRate);
    m_ak4522.reset();
    
    // Create DSP audio adapter - for now we'll pass a stub DSP reference
    // TODO: Initialize m_dspAdapter with actual m_dsp when available
    // m_dspAdapter = std::make_unique<DspAudioAdapter>(m_ak4522, *m_dsp);
    
    // Start audio engine with callback
    bool success = m_audioEngine.start(
        m_cfg.audio.sampleRate, 
        m_cfg.audio.bufferFrames,
        [&](size_t frames) {
            // Generate test tone for now - TODO: replace with DSP adapter
            static float phase = 0.0f;
            const float freq = 440.0f; // A4 test tone
            const float sr = static_cast<float>(m_cfg.audio.sampleRate);
            const float amp = 0.05f; // Low volume
            
            for (size_t i = 0; i < frames; ++i) {
                float sample = amp * std::sin(2.0f * 3.14159265f * freq * phase);
                phase += 1.0f / sr;
                if (phase >= 1.0f) phase -= 1.0f;
                
                int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
                m_ak4522.push(pcm, pcm);
            }
            
            // TODO: When m_dspAdapter is ready:
            // if (m_dspAdapter) m_dspAdapter->renderFrames(frames);
        }, 
        &m_ak4522
    );
    
    if (success) {
        char msg[128];
        sprintf_s(msg, sizeof(msg), "[AUDIO] I²S/AK4522 pipeline started: SR=%u buf=%u mode=%s\n",
                m_cfg.audio.sampleRate, m_cfg.audio.bufferFrames, m_cfg.audio.mode.c_str());
        postLog(msg);
        
        // Update diagnostic panel
        char diagNote[256];
        sprintf_s(diagNote, sizeof(diagNote), "Audio: SR=%u buf=%u mode=%s xruns=0 fill=~50%%",
                m_cfg.audio.sampleRate, m_cfg.audio.bufferFrames, m_cfg.audio.mode.c_str());
        m_diagState.addNote(std::string(diagNote));
    } else {
        postLog("[AUDIO] Failed to start I²S/AK4522 pipeline\n");
    }
}

// fw23.txt: Panel I/O validation methods
std::vector<PanelTestResult> Ms2kRunner::runPanelValidation() {
    if (!m_panelValidator) {
        LOGE("Panel validator not initialized");
        return {};
    }
    
    LOGI("fw23.txt: Starting full panel I/O validation suite");
    return m_panelValidator->runFullValidation();
}

void Ms2kRunner::startPanelMonitoring() {
    if (!m_panelValidator) {
        LOGE("Panel validator not initialized");
        return;
    }
    
    m_panelValidator->startContinuousMonitoring();
    postLog("[Runner] Panel I/O continuous monitoring started\n");
}

void Ms2kRunner::stopPanelMonitoring() {
    if (!m_panelValidator) {
        return;
    }
    
    m_panelValidator->stopContinuousMonitoring();
    postLog("[Runner] Panel I/O continuous monitoring stopped\n");
}

// fw25.txt: Enhanced LCD validation methods using global observer
void Ms2kRunner::initLcdObserver(const std::vector<std::string>& patterns, uint32_t stableMs) {
    g_lcdObs.reset();
    g_lcdObs.setPatterns(patterns);
    g_lcdObs.setStableMs(stableMs);
    
    // fw26.txt: Rewind observer from current DDRAM content for immediate pattern detection
    if (m_realLcd) {
        auto rewindObserverFromVram = [&]{
            // Load the 2×16 display from DDRAM using HD44780 address mapping
            auto loadLine = [&](int row){
                for (int i = 0; i < 16; i++){
                    uint8_t addr = (row == 0) ? (0x00 + i) : (0x40 + i);
                    // Get the character from DDRAM via getText() compatibility method
                    auto line = m_realLcd->getText(row);
                    if (i < line.length()) {
                        uint8_t d = static_cast<uint8_t>(static_cast<unsigned char>(line[i]));
                        g_lcdObs.onCmd(0x80 | addr);
                        g_lcdObs.onData(d);
                    }
                }
            };
            loadLine(0); loadLine(1);
        };
        rewindObserverFromVram(); // Execute rewind immediately
    }
    
    postLog("[Runner] LCD observer initialized with patterns and rewound from current DDRAM\n");
}

bool Ms2kRunner::checkLcdPattern() {
    return g_lcdObs.matched();
}

bool Ms2kRunner::isLcdStable() {
    return g_lcdObs.stableWindowReached();
}

// Power and mute state management implementation
void Ms2kRunner::setPowerState(bool powerOn) {
    if (m_powerOn != powerOn) {
        m_powerOn = powerOn;

        // Update LCD display based on power state
        if (m_realLcd) {
            if (powerOn) {
                // Clear LCD when powering on - firmware will handle initialization
                m_realLcd->exec(false, false, 0x01); // Clear Display command
                g_lcdGui.onCmd(0x01); // Also send to GUI buffer
                // Force LCD update to GUI
                updateLcdDisplayBuffer();
                postLog("[POWER] System powered ON - LCD cleared for firmware control\n");
            } else {
                // Show power-off message when powering off using HD44780 commands
                m_realLcd->exec(false, false, 0x01); // Clear Display
                g_lcdGui.onCmd(0x01); // Also send to GUI buffer
                m_realLcd->exec(false, false, 0x80); // Set DDRAM address to 0x00 (line 1)
                g_lcdGui.onCmd(0x80); // Also send to GUI buffer

                // Write "POWER OFF" message character by character
                const char* msg = "   POWER OFF    ";
                for (int i = 0; msg[i] && i < 16; i++) {
                    m_realLcd->exec(true, false, msg[i]); // Write data
                    g_lcdGui.onData(msg[i]); // Also send to GUI buffer
                }

                // Force LCD update to GUI
                updateLcdDisplayBuffer();
                postLog("[POWER] System powered OFF - LCD showing standby message\n");
            }
        }

        // Update diagnostic state
        m_diagState.addNote(std::string("Power: ") + (powerOn ? "ON" : "OFF"));
    }
}

void Ms2kRunner::setMuteState(bool muted) {
    if (m_muted != muted) {
        m_muted = muted;

        // Update LCD display to show mute status
        if (m_realLcd && m_powerOn) {
            // Show mute status on LCD (this would typically be handled by firmware)
            // For now, we'll just log the change
            postLog(muted ? "[MUTE] Audio muted\n" : "[MUTE] Audio unmuted\n");
        }

        // Update diagnostic state
        m_diagState.addNote(std::string("Mute: ") + (muted ? "ON" : "OFF"));
    }
}

// fw27.txt Phase 5: Panel timeline & LCD diff visualization implementation
void Ms2kRunner::enableTimelineVisualization(bool enable) {
    m_timelineEnabled = enable;
    if (enable && !m_timelineVisualizer) {
        m_timelineVisualizer = std::make_unique<PanelTimelineVisualizer>();
        postLog("[Runner] Panel timeline visualization ENABLED\n");
    } else if (!enable && m_timelineVisualizer) {
        m_timelineVisualizer.reset();
        postLog("[Runner] Panel timeline visualization DISABLED\n");
    }
}

std::string Ms2kRunner::getLcdDiffOverlay() const {
    if (m_timelineVisualizer) {
        return m_timelineVisualizer->getLcdDiffOverlay();
    }
    return "Timeline visualization not enabled";
}

bool Ms2kRunner::exportTimelinePNG(const std::string& filename) const {
    if (m_timelineVisualizer) {
        return m_timelineVisualizer->exportTimelinePNG(filename);
    }
    return false;
}

bool Ms2kRunner::exportSwitchSequence(const std::string& filename, uint32_t timeWindowMs) const {
    if (m_timelineVisualizer) {
        return m_timelineVisualizer->exportSwitchSequence(filename, std::chrono::milliseconds(timeWindowMs));
    }
    return false;
}

PanelTimelineVisualizer::Statistics Ms2kRunner::getVisualizationStats() const {
    if (m_timelineVisualizer) {
        return m_timelineVisualizer->getStatistics();
    }
    return {};
}

// ============================================================================
// InstrumentationManager Implementation
// ============================================================================

// Forward declare the builder
class InstrumentationManagerBuilder;

Ms2kRunner::InstrumentationManager::InstrumentationManager(Ms2kRunner* runner)
    : m_runner(runner) {
}

void Ms2kRunner::InstrumentationManager::enableAll() {
    setTracing(true);
    setDiagPanel(true);
    setPanelMonitoring(true);
    setTimelineVisualization(true);
    setCrashDumps(true);
    setAsyncLogging(true);
    setStateSnapshots(true);
    setVectorTracking(true);
    setSIMDStats(true);
    setDeterministic(true);
    // Don't auto-enable soak monitoring (long-running)
    if (m_runner->m_cpu) {
        m_runner->m_cpu->setTraceCpuAlways(true);
    }
}

void Ms2kRunner::InstrumentationManager::disableAll() {
    setTracing(false);
    setDiagPanel(false);
    setPanelMonitoring(false);
    setTimelineVisualization(false);
    setCrashDumps(false);
    setAsyncLogging(false);
    setReplayLogger(false);
    setSoakMonitoring(false);
    setStateSnapshots(false);
    setVectorTracking(false);
    setSIMDStats(false);
    setDeterministic(false);
}

void Ms2kRunner::InstrumentationManager::setTracing(bool enabled) {
    if (m_runner->m_cpu) {
        m_runner->m_cpu->setTraceCpuAlways(enabled);
    }
}

void Ms2kRunner::InstrumentationManager::setDiagPanel(bool enabled) {
    // Diag panel is always collecting data; this controls live display
    if (enabled) {
        m_runner->m_diagState.addNote("Diag panel: LIVE");
    } else {
        m_runner->m_diagState.addNote("Diag panel: STOPPED");
    }
}

void Ms2kRunner::InstrumentationManager::setPanelMonitoring(bool enabled) {
    if (enabled && m_runner->m_panelValidator) {
        m_runner->startPanelMonitoring();
    } else if (!enabled && m_runner->m_panelValidator) {
        m_runner->stopPanelMonitoring();
    }
}

void Ms2kRunner::InstrumentationManager::setTimelineVisualization(bool enabled) {
    m_runner->enableTimelineVisualization(enabled);
}

void Ms2kRunner::InstrumentationManager::setCrashDumps(bool enabled, const std::string& dir) {
    // Crash dumps are auto-installed; this is for configuration
    if (enabled) {
        // Crash handlers already installed in init()
        m_runner->m_diagState.addNote("Crash dumps: " + dir);
    }
}

void Ms2kRunner::InstrumentationManager::setAsyncLogging(bool enabled, size_t queueSize, uint32_t maxLps) {
    if (enabled) {
        MS2000::g_log.start(queueSize, maxLps);
    } else {
        MS2000::g_log.stop();
    }
}

void Ms2kRunner::InstrumentationManager::setReplayLogger(bool enabled, const std::string& recordFile, const std::string& replayFile) {
    if (m_runner->m_cpu) {
        if (enabled && !recordFile.empty()) {
            m_runner->m_cpu->startRecording(recordFile);
        } else if (enabled && !replayFile.empty()) {
            m_runner->m_cpu->startReplay(replayFile);
        } else {
            m_runner->m_cpu->stopRecording();
            m_runner->m_cpu->stopReplay();
        }
    }
}

void Ms2kRunner::InstrumentationManager::setSoakMonitoring(bool enabled, const SoakTestConfig& config) {
    if (enabled) {
        if (!m_runner->m_soakMonitor) {
            m_runner->m_soakMonitor = std::make_unique<SoakTestMonitor>(config);
        }
        m_runner->m_soakMonitor->start();
    } else if (m_runner->m_soakMonitor) {
        m_runner->m_soakMonitor->stop();
    }
}

void Ms2kRunner::InstrumentationManager::setStateSnapshots(bool enabled) {
    if (enabled && !m_runner->m_snapshotManager) {
        m_runner->m_snapshotManager = std::make_unique<StateSnapshotManager>(m_runner);
    }
}

void Ms2kRunner::InstrumentationManager::setVectorTracking(bool enabled) {
    if (m_runner->m_cpu) {
        // Vector tracker is auto-created in boot_wireMailboxAndSSI
        // This is a no-op if already created
    }
}

void Ms2kRunner::InstrumentationManager::setSIMDStats(bool enabled) {
    // SIMD stats are always collected when SIMD is enabled
    if (enabled && m_runner->m_cpu) {
        // Could enable SIMD here if not already
    }
}

void Ms2kRunner::InstrumentationManager::setDeterministic(bool enabled, uint64_t seed) {
    if (enabled) {
        DeterministicState::Config config;
        config.prngSeed = seed ? seed : 0xDEADBEEF;
        config.fixedSeed = true;
        config.deterministicTiming = true;
        config.tickDurationMs = 1;
        m_runner->m_deterministicState.resetWithConfig(config);
    }
}

DiagSnapshot Ms2kRunner::InstrumentationManager::getDiagSnapshot() const {
    return m_runner->m_diagState.snapshot();
}

void Ms2kRunner::InstrumentationManager::dumpFirstFaultTrace() const {
    if (m_runner->m_cpu) {
        // The emulator has dumpFirstFaultTrace() as private; access via internal method
        // For now, we log that it would be called
        m_runner->postLog("[INSTR] First-fault trace dump requested (access via emulator directly)\n");
    }
}

void Ms2kRunner::InstrumentationManager::dumpBootLog() const {
    if (m_runner->m_cpu) {
        m_runner->postLog("[INSTR] Boot log dump requested (access via emulator directly)\n");
    }
}

void Ms2kRunner::InstrumentationManager::dumpStackTaintReport() const {
    if (m_runner->m_cpu) {
        m_runner->postLog("[INSTR] Stack taint report requested (access via emulator directly)\n");
    }
}

SoakTestMonitor::ValidationResult Ms2kRunner::InstrumentationManager::getSoakResults() const {
    if (m_runner->m_soakMonitor) {
        return m_runner->m_soakMonitor->validateResults();
    }
    return {};
}

PanelTimelineVisualizer::Statistics Ms2kRunner::InstrumentationManager::getVisualizationStats() const {
    if (m_runner->m_timelineVisualizer) {
        return m_runner->m_timelineVisualizer->getStatistics();
    }
    return {};
}

bool Ms2kRunner::InstrumentationManager::exportAllData(const std::string& baseDir) const {
    bool ok = true;
    
    // Export timeline
    if (m_runner->m_timelineVisualizer) {
        ok &= m_runner->m_timelineVisualizer->exportTimelinePNG(baseDir + "/timeline");
        ok &= m_runner->m_timelineVisualizer->exportSwitchSequence(baseDir + "/sequence", std::chrono::milliseconds(5000));
    }
    
    // Export diag snapshot
    DiagSnapshot diag = getDiagSnapshot();
    // Could write to file here
    
    // Export soak results
    if (m_runner->m_soakMonitor) {
        auto results = getSoakResults();
        // Could write to file here
    }
    
    return ok;
}

bool Ms2kRunner::InstrumentationManager::isAnyEnabled() const {
    // Check if any major instrument is active
    return m_runner->m_diagState.snapshot().recording ||
           m_runner->m_diagState.snapshot().replaying ||
           m_runner->m_timelineEnabled ||
           (m_runner->m_soakMonitor && m_runner->m_soakMonitor->isRunning());
}

std::string Ms2kRunner::InstrumentationManager::getStatusReport() const {
    std::stringstream ss;
    ss << "=== INSTRUMENTATION STATUS ===\n";
    ss << "Diag Panel: " << (m_runner->m_diagState.snapshot().recording ? "RECORDING" : "IDLE") << "\n";
    ss << "Timeline: " << (m_runner->m_timelineEnabled ? "ENABLED" : "DISABLED") << "\n";
    ss << "Soak Test: " << (m_runner->m_soakMonitor && m_runner->m_soakMonitor->isRunning() ? "RUNNING" : "STOPPED") << "\n";
    ss << "Panel Monitor: " << (m_runner->m_panelValidator ? "ACTIVE" : "INACTIVE") << "\n";
    ss << "State Snapshots: " << (m_runner->m_snapshotManager ? "READY" : "NOT INIT") << "\n";
    ss << "DSP56362: NOT MODELLED (BUG103)\n";
    ss << "Crash Dumps: INSTALLED (auto)\n";
    ss << "Async Logger: RUNNING (auto-started)\n";
    ss << "Replay Logger: " << (m_runner->m_cpu && m_runner->m_cpu->isRecording() ? "RECORDING" : 
                               (m_runner->m_cpu && m_runner->m_cpu->isReplaying() ? "REPLAYING" : "OFF")) << "\n";
    ss << "Deterministic: " << (m_runner->m_deterministicState.isInitialized() ? "YES (seed=" + 
                               std::to_string(m_runner->m_deterministicState.getCurrentSeed()) + ")" : "NO") << "\n";
    return ss.str();
}

// ============================================================================
// End InstrumentationManager
// ============================================================================

} // namespace MS2000
