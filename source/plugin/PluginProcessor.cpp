// MS2000R VST3 (phase 1, 2026-09-28). The emulator driven by the host's audio callback through
// Ms2kRunner::render() (ENGINE-BLOCK): the DSP's 48 kHz frames resampled to the host rate, MIDI in at its
// frame offset, MIDI OUT (the firmware's TDR1 stream) regrouped into messages and handed back to the host.
//
// STATED for phase 1:
//  - one machine per copy of this binary (the emulator core keeps function-local statics and globals); MULTI-1
//    (2026-10-02): the bundle's loader module (shim/ms2k_vst3_shim.cpp) gives every further instance its own copy;
//  - the emulator opens its files by relative path, so the plugin makes the MS2000 folder the process's
//    working directory: MS2K_HOME, else Documents\MS2000R, else a folder above the plugin binary - the first holding flash.bin;
//  - audio input (vocoder / Audio In) is passed through only at a host rate of 48 kHz;
//  - no plugin state yet: the machine keeps its own (flash state / NVRAM files in that folder).
//
// Phase 2 (VST3-2, 2026-09-28), STATED:
//  - the project holds the machine: the flash sectors that are not factory any more (Global WRITE, program
//    WRITE - FlashROM::stateMask), the 32 knob positions, VR30/VR31, SW1, the DAC option, the volume and the
//    editor's size. A fresh instance (no project state) powers on with the MS2000 folder's flash state, as
//    the standalone does; the plugin never writes that folder's flash state files - its own lives in the project;
//  - a project state with a different flash, arriving while the machine runs, is a power cycle: the machine
//    is stopped and started again with that flash (processBlock plays silence meanwhile);
//  - Audio In at any host rate: the inputs are resampled host rate -> 48 kHz (Lagrange), as the output is.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>
#include <cmath>

namespace {
std::atomic<bool> g_machineTaken{ false };
std::atomic<int> g_liveProcessors{ 0 };   // MULTI-1: read by the loader module (ms2k_liveProcessors)

bool validHome(const juce::File& d)
{
    // BOOTROM-1: the public boot-362.bin is enough - the emulator corrects its two words in memory
    const auto fw = d.getChildFile("full FW");
    return d.getChildFile("flash.bin").existsAsFile()
        && (fw.getChildFile("boot-362.ms2000.bin").existsAsFile() || fw.getChildFile("boot-362.bin").existsAsFile());
}
// HOME-1 (2026-10-01): the folder picked in Settings, kept for every instance and project
juce::File homeSetting() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MS2000R").getChildFile("home.txt"); }

juce::File findHome()
{
    juce::StringArray cands;
    if (const char* e = std::getenv("MS2K_HOME"); e && *e) cands.add(e);
    if (homeSetting().existsAsFile()) cands.add(homeSetting().loadFileAsString().trim());
    cands.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MS2000R").getFullPathName());
    // PUBLIC-1: the folders above the plugin binary (...\X\VST3\MS2000R.vst3\Contents\x86_64-win\MS2000R.vst3 -> X)
    // MULTI-1: an engine copy runs from %TEMP%; the loader module names the real plugin binary in MS2K_PLUGIN_BINARY
    const auto loader = juce::SystemStats::getEnvironmentVariable("MS2K_PLUGIN_BINARY", {});
    const juce::File binary = loader.isNotEmpty() ? juce::File(loader) : juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (auto d = binary.getParentDirectory(); d.exists() && cands.size() < 16; d = d.getParentDirectory()) {
        cands.add(d.getFullPathName());
        if (d.isRoot()) break;
    }
    for (auto& c : cands) {
        juce::File d(c);
        if (validHome(d)) return d;
    }
    return {};
}
} // namespace

Ms2kProcessor::Ms2kProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Audio In", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    ++g_liveProcessors;
    for (auto& m : knobs) for (auto& v : m) v = 512;     // every pot at its centre, as the standalone's default
    loadGlobalSettings();                                // SETTINGS-1
    // AUTOMATION-1: one host parameter per physical control (panel_params.h)
    for (auto& r : m_knobParam) for (auto& v : r) v = -1;
    for (auto& r : m_keyParam) for (auto& v : r) v = -1;
    for (int i = 0; i < ms2kparams::kCount; ++i) {
        const auto& d = ms2kparams::kParams[i];
        const juce::ParameterID pid{ d.id, 1 };
        juce::RangedAudioParameter* p = nullptr;
        if (d.kind == ms2kparams::kKey || d.kind == ms2kparams::kMic) p = new juce::AudioParameterBool(pid, d.name, false);
        else p = new juce::AudioParameterFloat(pid, d.name, juce::NormalisableRange<float>(0.0f, 1.0f), d.kind == ms2kparams::kKnob ? 512.0f / 1023.0f : 1.0f);
        addParameter(p);
        m_params[size_t(i)] = p;
        if (d.kind == ms2kparams::kKnob) m_knobParam[d.a][d.b] = i;
        if (d.kind == ms2kparams::kKey) m_keyParam[d.a][d.b] = i;
    }
    syncParams();
    m_syx.setSend([this](const uint8_t* p, size_t n) { if (m_runner) m_runner->sendMIDIData(p, n); });
    m_syx.setClock([this] { return double(m_frames.load(std::memory_order_relaxed)) / (m_hostRate > 0 ? m_hostRate : 48000.0); });   // machine time
    startTimerHz(10);
}

// SYX-1: the .syx tool runs on the message thread. R2 diagnostics (default OFF): MS2K_SYXIMPORT=<file> loads it
// once the machine has played 5 s, then MS2K_SYXEXPORT=<file> saves all programs; results on stdout.
// HOSTDIAG-1: diagnostics go to a FILE (%TEMP%\ms2k_hostdiag.log), never to OutputDebugString - that call
// blocks until a debugger output viewer takes the line, and a stalled DebugView froze the host's message
// thread inside our timer (measured: vsthost main thread in OutputDebugStringA -> WaitForSingleObject).
static void ms2kDiagLog(const juce::String& line)
{
    static const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ms2k_hostdiag.log");
    f.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " " + line + "\n", false, false, "\n");
}

void Ms2kProcessor::timerCallback()
{
    m_syx.poll();
    powerSwitchPoll();
    // HOSTDIAG-1 (R2, default off): MS2K_HOSTDIAG=1 -> once a second to the debugger output (DebugView): is the
    // host still calling processBlock, does the machine advance, and does render() hand out silence. For the
    // VSTHost-only freeze after Demo -> EXIT (2026-10-08): all threads idle-wait in the dump, no spinner.
    if (static const bool diag = [] { const char* e = std::getenv("MS2K_HOSTDIAG"); return e && *e && *e != '0'; }();
        diag && m_runner && ++m_hostDiagTick >= 10) {
        m_hostDiagTick = 0;
        auto& cpu = m_runner->getEmulator();   // read unsynchronised: a diagnostic, values may be a moment stale
        ms2kDiagLog(juce::String::formatted("[MS2K-HOSTDIAG] blocks=%llu frames=%llu silence=%llu h8pc=%06X h8cyc=%llu",
            (unsigned long long)m_blocks.load(), (unsigned long long)m_frames.load(),
            (unsigned long long)m_runner->renderSilenceFrames(), unsigned(cpu.getProgramCounter()),
            (unsigned long long)cpu.getCycles()));
        juce::String rt;
        for (int i = 0; i < 8; ++i) if (const auto c = m_diagRt[i].load()) rt << juce::String::toHexString(0xF8 + i).toUpperCase() << "x" << juce::String((juce::int64)c) << " ";
        if (rt.isNotEmpty()) ms2kDiagLog("[MS2K-MIDIOUT] realtime totals: " + rt);
        {   // FREEZE-PC hunt: the keys the panel holds down (a stuck key blocks the program pads) and the LCD
            juce::String held;
            for (unsigned c = 0; c < 8; ++c) for (unsigned r = 0; r < 8; ++r) if (m_swHeld[c][r]) held << int(c) << "," << int(r) << " ";
            const LcdGuiSnapshot s = m_runner->getLcdGuiSnapshot();
            ms2kDiagLog("[MS2K-PANEL] held=" + (held.isEmpty() ? juce::String("-") : held) + " lcd=\"" + juce::String(s.line0) + "|" + juce::String(s.line1) + "\"");
        }
        const uint32_t w = m_diagOutW.load(std::memory_order_acquire);
        if (w - m_diagOutR > 256) m_diagOutR = w - 256;
        for (int n = 0; m_diagOutR != w && n < 40; ++m_diagOutR, ++n) {
            const auto& m = m_diagOut[m_diagOutR & 255];
            juce::String h; for (int i = 0; i < m.len && i < 7; ++i) h << juce::String::toHexString(m.b[i]).paddedLeft('0', 2).toUpperCase() << " ";
            ms2kDiagLog("[MS2K-MIDIOUT] blk=" + juce::String((juce::int64)m.blk) + " len=" + juce::String(int(m.len)) + " " + h);
        }
    }
    static const char* imp = std::getenv("MS2K_SYXIMPORT");
    static const char* exp = std::getenv("MS2K_SYXEXPORT");
    static const char* prg = std::getenv("MS2K_SYXPROGRAM");   // LIBRARY-1 diag: "<file>|<index>" after 5 s
    if (prg && m_syxDiag == 0 && m_frames.load() > uint64_t(5.0 * m_hostRate)) {
        m_syxDiag = 1;
        const std::string a(prg); const auto bar = a.rfind('|');
        std::vector<MS2000::SyxTool::Program> pr; std::string err;
        if (bar != std::string::npos && MS2000::SyxTool::parsePrograms(a.substr(0, bar), pr, err)) {
            const size_t k = size_t(std::atoi(a.c_str() + bar + 1));
            std::printf("[SYX] library: %zu programs, sending #%zu '%s'\n", pr.size(), k, k < pr.size() ? pr[k].name.c_str() : "?");
            if (k < pr.size()) m_syx.startProgram(pr[k].data, pr[k].name);
        } else std::printf("[SYX] library: %s\n", err.c_str());
    }
    if (prg && m_syxDiag == 1 && !m_syx.busy()) { m_syxDiag = 3; std::printf("[SYX] %s\n", m_syx.status().c_str()); std::fflush(stdout); }
    if (!imp && !exp) return;
    static uint64_t f0 = 0;
    if (m_syxDiag == 0 && m_frames.load() > uint64_t(5.0 * m_hostRate)) { m_syxDiag = 1; f0 = m_frames.load(); if (imp) m_syx.startImport(imp); }
    else if (m_syxDiag == 1 && !m_syx.busy()) { m_syxDiag = 2; if (imp) std::printf("[SYX] %s (machine time %.1f s)\n", m_syx.status().c_str(), double(m_frames.load() - f0) / m_hostRate); if (exp) m_syx.startExport(exp); }
    else if (m_syxDiag == 2 && !m_syx.busy()) { m_syxDiag = 3; if (exp) std::printf("[SYX] %s\n", m_syx.status().c_str()); std::fflush(stdout); }
}

Ms2kProcessor::~Ms2kProcessor()
{
    stopTimer();
    stopMachine();
    if (m_owner) g_machineTaken = false;
    --g_liveProcessors;
}

// MULTI-1: how many machines this copy of the binary holds; the loader module (shim/ms2k_vst3_shim.cpp) gives a new
// instance a copy where this is 0
extern "C" __declspec(dllexport) int ms2k_liveProcessors() { return g_liveProcessors.load(); }

void Ms2kProcessor::stopMachine()
{
    if (m_runner) {
        m_runner->getEmulator().setMidiOutSink(nullptr);
        m_runner->stop();
        m_runner.reset();
    }
}

void Ms2kProcessor::setKnob(unsigned mux, unsigned x, uint16_t v)
{
    if (mux >= 4 || x >= 8) return;
    knobs[mux][x] = v;
    if (m_runner) m_runner->getEmulator().setPanelKnob(mux, x, v);
}

void Ms2kProcessor::setSwitch(unsigned col, unsigned row, bool down)
{
    if (col < 8 && row < 8) m_swHeld[col][row] = down;
    if (m_runner) m_runner->getEmulator().setPanelSwitch(col, row, down);
}

void Ms2kProcessor::applyInputStage()
{
    if (auto* d = m_runner ? m_runner->getEmulator().dsp() : nullptr) d->setInputStage(in1, in2, mic2);
}

void Ms2kProcessor::applyDac()
{
    if (auto* d = m_runner ? m_runner->getEmulator().dsp() : nullptr) d->setDac20(dac20);
}

void Ms2kProcessor::applyPanel()
{
    if (!m_runner) return;
    for (unsigned m = 0; m < 4; ++m) for (unsigned x = 0; x < 8; ++x) m_runner->getEmulator().setPanelKnob(m, x, knobs[m][x]);
    applyInputStage(); applyDac(); applyVolume();
    for (unsigned c = 0; c < 8; ++c) for (unsigned r = 0; r < 8; ++r)
        if (m_swHeld[c][r]) m_runner->getEmulator().setPanelSwitch(c, r, true);
}

void Ms2kProcessor::clearStreams()
{
    m_srcL.clear(); m_srcR.clear(); m_interpL.reset(); m_interpR.reset();
    m_outBytes.clear(); m_outStatus = 0; m_outHave = 0; m_outSysex = false; m_sysex.clear();
    for (int ch = 0; ch < 2; ++ch) { m_inFifo[ch].clear(); m_inInterp[ch].reset(); }
}

// ---- AUTOMATION-1 ----
float Ms2kProcessor::panelValue(int i) const
{
    const auto& d = ms2kparams::kParams[i];
    switch (d.kind) {
    case ms2kparams::kKnob:   return float(knobs[d.a][d.b]) / 1023.0f;
    case ms2kparams::kKey:    return m_swHeld[d.a][d.b] ? 1.0f : 0.0f;
    case ms2kparams::kVolume: return volume;
    case ms2kparams::kIn1:    return in1;
    case ms2kparams::kIn2:    return in2;
    case ms2kparams::kMic:    return mic2 ? 1.0f : 0.0f;
    }
    return 0.0f;
}

void Ms2kProcessor::syncParams()
{
    for (int i = 0; i < ms2kparams::kCount; ++i) {
        const float v = panelValue(i);
        m_applied[size_t(i)].store(v);
        if (std::abs(m_params[size_t(i)]->getValue() - v) > 1.0e-6f) m_params[size_t(i)]->setValueNotifyingHost(v);
    }
}

// Audio thread. live = the machine lock is held and a machine runs; otherwise the control is only stored (the next
// power-on / reboot sets it into the machine with applyPanel).
void Ms2kProcessor::pollParams(bool live)
{
    bool stage = false;
    for (int i = 0; i < ms2kparams::kCount; ++i) {
        const float v = m_params[size_t(i)]->getValue();
        if (std::abs(v - m_applied[size_t(i)].load(std::memory_order_relaxed)) <= 1.0e-6f) continue;
        m_applied[size_t(i)].store(v, std::memory_order_relaxed);
        const auto& d = ms2kparams::kParams[i];
        switch (d.kind) {
        case ms2kparams::kKnob: {
            const uint16_t q = uint16_t(juce::jlimit(0, 1023, int(v * 1023.0f + 0.5f)));
            if (live) setKnob(d.a, d.b, q); else knobs[d.a][d.b] = q;
            break; }
        case ms2kparams::kKey:
            if (live) setSwitch(d.a, d.b, v >= 0.5f); else m_swHeld[d.a][d.b] = v >= 0.5f;
            break;
        case ms2kparams::kVolume: volume = v; applyVolume(); break;
        case ms2kparams::kIn1: in1 = v; stage = true; break;
        case ms2kparams::kIn2: in2 = v; stage = true; break;
        case ms2kparams::kMic: mic2 = v >= 0.5f; stage = true; break;
        }
    }
    if (stage && live) applyInputStage();
}

void Ms2kProcessor::userParam(int i, float v)
{
    if (i < 0) return;
    auto* p = m_params[size_t(i)];
    if (!m_gesture[size_t(i)]) { p->beginChangeGesture(); m_gesture[size_t(i)] = true; m_anyGesture = true; }
    m_applied[size_t(i)].store(v);
    p->setValueNotifyingHost(v);
}

void Ms2kProcessor::userKnob(unsigned mux, unsigned x, uint16_t v)
{
    setKnob(mux, x, v);
    if (mux < 4 && x < 8) userParam(m_knobParam[mux][x], float(v) / 1023.0f);
}

void Ms2kProcessor::userKey(unsigned col, unsigned row, bool down)
{
    setSwitch(col, row, down);
    if (col < 8 && row < 8) userParam(m_keyParam[col][row], down ? 1.0f : 0.0f);
}

void Ms2kProcessor::userVolume()
{
    applyVolume();
    for (int i = 0; i < ms2kparams::kCount; ++i)
        if (ms2kparams::kParams[i].kind == ms2kparams::kVolume) userParam(i, volume);
}

void Ms2kProcessor::userInputs()
{
    applyInputStage();
    for (int i = 0; i < ms2kparams::kCount; ++i) {
        const auto k = ms2kparams::kParams[i].kind;
        if (k == ms2kparams::kIn1 || k == ms2kparams::kIn2 || k == ms2kparams::kMic) {
            const float v = panelValue(i);
            if (std::abs(m_params[size_t(i)]->getValue() - v) > 1.0e-6f) userParam(i, v);
        }
    }
}

void Ms2kProcessor::endUserEdits()
{
    if (!m_anyGesture) return;
    for (int i = 0; i < ms2kparams::kCount; ++i)
        if (m_gesture[size_t(i)]) { m_params[size_t(i)]->endChangeGesture(); m_gesture[size_t(i)] = false; }
    m_anyGesture = false;
}

// PWR-SW-1: the Master VR's switch. Off: the flash is kept (the project's state from now on, the chip image in a
// DEV-FLASH build), the machine stops - dark LCD, silence. On: a cold boot from that flash, held keys down.
void Ms2kProcessor::powerSwitchPoll()
{
    if (!m_owner) return;
    const bool open = powerSwitchOpen();
    if (open && m_runner && !m_poweredOff) {
        std::lock_guard<std::mutex> l(m_machineMx);
        const auto& f = m_runner->getEmulator().getFlashROM();
        m_projectMask = f.stateMask();
        m_projectImage.assign(f.data(), f.data() + f.size());
        m_haveProjectFlash = true;
        stopMachine();
        clearStreams();
        m_poweredOff = true;
        setStatus("POWER off (Master VOLUME at its minimum)");
    } else if (!open && m_poweredOff) {
        std::lock_guard<std::mutex> l(m_machineMx);
        m_poweredOff = false;
        clearStreams();
        startMachine();
    }
}

bool Ms2kProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

bool Ms2kProcessor::bootMachine()
{
    m_bootTried = true;
    if (g_machineTaken.exchange(true)) { setStatus("Only one MS2000R can run in a process."); return false; }
    m_owner = true;
    m_home = findHome();
    if (m_home == juce::File()) {
        setStatus("The MS2000 folder (flash.bin + full FW\\boot-362.bin) was not found - Settings: Choose the MS2000 folder.");
        return false;
    }
    m_home.setAsCurrentWorkingDirectory();
    if (!std::getenv("MS2K_MODEL")) _putenv_s("MS2K_MODEL", "R");
    if (powerSwitchOpen()) { m_poweredOff = true; setStatus("POWER off (Master VOLUME at its minimum)"); return true; }
    return startMachine();
}

// HOME-1: the folder chosen in Settings - saved for every instance; powers the machine on if it is not running yet
bool Ms2kProcessor::chooseHome(const juce::File& d)
{
    if (!validHome(d)) { setStatus("No flash.bin + full FW\\boot-362.bin in " + d.getFullPathName()); return false; }
    homeSetting().getParentDirectory().createDirectory();
    homeSetting().replaceWithText(d.getFullPathName());
    if (m_runner) { setStatus("Saved: " + d.getFullPathName() + " - used from the next start"); return true; }
    if (!m_bootTried) { setStatus("Saved: " + d.getFullPathName()); return true; }   // prepareToPlay boots from it
    if (!m_owner) {
        if (g_machineTaken.exchange(true)) { setStatus("Only one MS2000R can run in a process."); return false; }
        m_owner = true;
    }
    std::lock_guard<std::mutex> l(m_machineMx);
    m_home = d;
    m_home.setAsCurrentWorkingDirectory();
    if (!std::getenv("MS2K_MODEL")) _putenv_s("MS2K_MODEL", "R");
    return startMachine();
}

bool Ms2kProcessor::startMachine()
{

    MS2000::Ms2kConfig cfg;
    cfg.romPath = "flash.bin";
    cfg.cpuCyclesPerTick = 20000;
    cfg.sampleRate = 48000;
    cfg.quietBoot = true;
    cfg.useMmcss = false;
    cfg.audio.enabled = false;
    cfg.dspThread = dspThread.load();                       // DSP-THREAD: optional (Settings) - more total CPU, less on the host's audio thread
    cfg.flashStateSave = false;                              // the plugin's flash state lives in the project
    cfg.flashStateLoad = !m_haveProjectFlash;                // a fresh instance: the folder's, as the standalone
    if (m_haveProjectFlash && m_projectMask) { cfg.flashStateMask = m_projectMask; cfg.flashStateImage = m_projectImage; }
#if MS2K_DEV_FLASH
    cfg.devFlashPath = "MBM29LV800BA.bin";   // DEV-FLASH-1: developer build - the whole chip in the MS2000 folder
    cfg.flashStateLoad = false; cfg.flashStateMask = 0; cfg.flashStateImage.clear();
#endif
    MS2000::Ms2kGuiHooks hooks;
    hooks.logFn = [](const char*) {};
    m_runner = std::make_unique<MS2000::Ms2kRunner>(cfg, hooks);
    if (!m_runner->init() || !m_runner->startManual()) {
        setStatus("The machine did not start (ROM load / init failed).");
        m_runner.reset();
        return false;
    }
    m_runner->getEmulator().setMidiOutSink([this](uint8_t b) {
        auto* d = m_runner ? m_runner->getEmulator().dsp() : nullptr;
        m_outBytes.push_back({ d ? d->txFrames() : 0, b });   // called on the audio thread, inside render()
        m_syx.feed(b);
    });
    applyPanel();
    const uint32_t fm = m_runner->getEmulator().getFlashROM().stateMask();
    setStatus("running - " + m_home.getFullPathName() + (m_haveProjectFlash ? "  (flash from the project" : "  (flash from the folder")
              + (fm ? ", sectors " + juce::String::toHexString(int(fm)).toUpperCase() + ")" : ", factory)"));
    return true;
}

void Ms2kProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_hostRate = sampleRate > 0 ? sampleRate : 48000.0;
    const size_t cap = size_t(std::ceil(double(samplesPerBlock) * 48000.0 / m_hostRate)) + 64;
    m_srcL.reserve(cap * 2); m_srcR.reserve(cap * 2);
    m_tmpL.resize(cap); m_tmpR.resize(cap); m_tmpFlags.resize(cap);
    m_midiIn.reserve(256); m_outBytes.reserve(1024);
    m_interpL.reset(); m_interpR.reset();
    m_srcL.clear(); m_srcR.clear();
    for (int ch = 0; ch < 2; ++ch) { m_inInterp[ch].reset(); m_inFifo[ch].clear(); m_inFifo[ch].reserve(size_t(samplesPerBlock) * 2 + 64); }
    m_inTmp.resize(cap);
    if (!m_bootTried) bootMachine();
}

void Ms2kProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    m_blocks.fetch_add(1, std::memory_order_relaxed);   // HOSTDIAG-1
    const int N = buffer.getNumSamples();
    std::unique_lock<std::mutex> machine(m_machineMx, std::try_to_lock);   // a reboot holds it: silence
    pollParams(machine.owns_lock() && m_runner);                            // AUTOMATION-1: host automation -> panel
    if (!machine.owns_lock() || !m_runner || N <= 0) { buffer.clear(); midi.clear(); return; }
    auto* dsp = m_runner->getEmulator().dsp();

    // DEMO-FMT-1 diag (off unless set): MS2K_DEMOAT=<s> presses the demo keys at that machine time, as the Settings
    // button does (EXIT, GLOBAL 200 ms later, both up after 1.4 s) - in machine time, so a run is repeatable
    if (static const char* demoAt = std::getenv("MS2K_DEMOAT"); demoAt && m_demoDiag < 3) {
        const double t = double(m_frames.load(std::memory_order_relaxed)) / m_hostRate, t0 = std::atof(demoAt);
        if (m_demoDiag == 0 && t >= t0) { setSwitch(4, 0, true); m_demoDiag = 1; }
        else if (m_demoDiag == 1 && t >= t0 + 0.2) { setSwitch(3, 6, true); m_demoDiag = 2; }
        else if (m_demoDiag == 2 && t >= t0 + 1.4) { setSwitch(3, 6, false); setSwitch(4, 0, false); m_demoDiag = 3; }
    }

    // PADSTORM diag (off unless set; FREEZE-PC hunt 2026-10-08): MS2K_PADSTORM=<start s>,<period ms>,<hold ms>,<seed>
    // presses a pseudo-random program pad 1-16 every period (machine time) and logs, per press, the LCD, the H8 PC
    // and the wall time the last period took, to %TEMP%\ms2k_padstorm.log - a stuck program change shows there.
    if (static const char* ps = std::getenv("MS2K_PADSTORM"); ps) {
        static double s0 = 3.0, per = 150.0, hold = 50.0; static unsigned seed = 1;
        static const bool parsed = std::sscanf(ps, "%lf,%lf,%lf,%u", &s0, &per, &hold, &seed) > 0;
        (void)parsed;
        const double t = double(m_frames.load(std::memory_order_relaxed)) / m_hostRate;
        if (t >= s0) {
            const long n = long((t - s0) * 1000.0 / per);
            const double ph = (t - s0) * 1000.0 - double(n) * per;
            auto padSw = [this](int k, bool d) { setSwitch(k < 8 ? 6u : 5u, 7u - unsigned(k % 8), d); };
            if (n != m_stormN) {
                if (m_stormDown) padSw(m_stormPad, false);
                seed = seed * 1103515245u + 12345u;
                const int k = int((seed >> 16) % 16u);
                const LcdGuiSnapshot s = m_runner->getLcdGuiSnapshot();
                char l0[17], l1[17];
                for (int i = 0; i < 16; ++i) { l0[i] = s.line0[i] >= 0x20 ? s.line0[i] : '.'; l1[i] = s.line1[i] >= 0x20 ? s.line1[i] : '.'; }
                l0[16] = l1[16] = 0;
                const double now = juce::Time::getMillisecondCounterHiRes();
                static const std::string logPath = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\ms2k_padstorm.log";
                if (FILE* f = std::fopen(logPath.c_str(), "a")) {
                    std::fprintf(f, "%9.3f pad %2d | %s | %s | pc %06X | wall %7.1f ms\n", t, k + 1, l0, l1,
                                 unsigned(m_runner->getEmulator().getProgramCounter()), m_stormWall > 0 ? now - m_stormWall : 0.0);
                    std::fclose(f);
                }
                m_stormWall = now;
                padSw(k, true);
                m_stormPad = k; m_stormN = n; m_stormDown = true;
            } else if (m_stormDown && ph >= hold) {
                padSw(m_stormPad, false); m_stormDown = false;
            }
        }
    }

    // DEMO-FMT-1 diag (off unless set): MS2K_RAMDUMPAT=<s>[,<s>...]|<prefix> writes the DRAM (0x400000-0x47FFFF)
    // to <prefix>_<n>.bin at those machine times
    if (static const char* rd = std::getenv("MS2K_RAMDUMPAT"); rd) {
        const std::string a(rd); const auto bar = a.find('|');
        std::vector<double> at; for (size_t p = 0; p < bar;) { at.push_back(std::atof(a.c_str() + p)); p = a.find(',', p); if (p == std::string::npos || p > bar) break; ++p; }
        const double t = double(m_frames.load(std::memory_order_relaxed)) / m_hostRate;
        if (bar != std::string::npos && m_ramDumps < int(at.size()) && t >= at[size_t(m_ramDumps)]) {
            std::vector<uint8_t> ram(0x80000);
            for (uint32_t i = 0; i < 0x80000u; ++i) ram[i] = m_runner->getEmulator().peekExternal(0x400000u + i);
            if (FILE* f = std::fopen((a.substr(bar + 1) + "_" + std::to_string(m_ramDumps) + ".bin").c_str(), "wb")) { std::fwrite(ram.data(), 1, ram.size(), f); std::fclose(f); }
            ++m_ramDumps;
        }
    }

    // Audio In: input 1 = left, input 2 = right; at another host rate resampled to 48 kHz first (VST3-2).
    const int nIn = getTotalNumInputChannels();
    if (dsp && nIn > 0) {
        if (std::abs(m_hostRate - 48000.0) < 0.5) {
            dsp->pushAudioIn(0, buffer.getReadPointer(0), uint32_t(N));
            dsp->pushAudioIn(1, buffer.getReadPointer(nIn > 1 ? 1 : 0), uint32_t(N));
        } else {
            const double speed = m_hostRate / 48000.0;   // host samples per 48 kHz frame
            for (int ch = 0; ch < 2; ++ch) {
                auto& q = m_inFifo[ch];
                const float* in = buffer.getReadPointer(ch < nIn ? ch : 0);
                q.insert(q.end(), in, in + N);
                const int nOut = int(double(q.size()) / speed) - 2;
                if (nOut <= 0) continue;
                if (m_inTmp.size() < size_t(nOut)) m_inTmp.resize(size_t(nOut));
                size_t used = size_t(m_inInterp[ch].process(speed, q.data(), m_inTmp.data(), nOut));
                if (used > q.size()) used = q.size();
                q.erase(q.begin(), q.begin() + ptrdiff_t(used));
                dsp->pushAudioIn(ch, m_inTmp.data(), uint32_t(nOut));
            }
        }
    }

    const double ratio = 48000.0 / m_hostRate;
    const size_t have = m_srcL.size();
    const size_t need = size_t(std::ceil(double(N) * ratio)) + 4;
    const uint32_t toRender = need > have ? uint32_t(need - have) : 0u;
    if (m_tmpL.size() < toRender) { m_tmpL.resize(toRender); m_tmpR.resize(toRender); m_tmpFlags.resize(toRender); }

    // MIDI-CLOCK (2026-10-01, Tamas: with Global MIDI Clock = Ext the tempo went to 0 - a VST3 host sends no MIDI
    // clock, only its tempo): the host's tempo as F8 timing clocks, 24 per quarter, into MIDI IN at their sample.
    // Phase-locked to the host's ppq position while it plays; free-running at its tempo while it is stopped, so
    // the arpeggiator keeps time either way. Settings: on/off.
    // MIDI-CLOCK b (Tamas: the beat position too): the MS2000 receives F8, FA Start and FC Stop ("Arpeggiator stop")
    // with Clock = External/Auto - no Song Position (MIDI Implementation, RECOGNIZED REALTIME). So when the host
    // starts playing (or jumps, e.g. a loop), FA goes out AT ONCE, with a clock at the first sample of the block - the
    // clock phase is snapped to the host's position (to the nearest clock, < half a clock = 1/48 quarter) - so the
    // machine's beat 1 is where the host started (Tamas: waiting for the next quarter made the arp lag a beat);
    // when the host stops, FC. Settings.
    if (hostClock) {
        double bpm = 0.0, ppq = 0.0; bool playing = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) {
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto p = pos->getPpqPosition(); p && pos->getIsPlaying()) { ppq = *p; playing = true; }
            }
        if (bpm > 1.0) {
            const double tps = bpm / 60.0 * 24.0 / m_hostRate;            // clocks per host sample
            double t0 = playing ? ppq * 24.0 : m_clockTick;
            static const uint8_t f8 = 0xF8, fa = 0xFA, fc = 0xFC;
            const bool tr = transportMsgs.load();
            const bool start = playing && tr && (!m_wasPlaying || std::abs(t0 - m_clockTick) > 1.0);   // start or jump
            if (!playing && m_wasPlaying && tr) midi.addEvent(&fc, 1, 0);
            if (start) { t0 = std::round(t0); midi.addEvent(&fa, 1, 0); }   // beat 1 = this clock, now
            const double t1 = t0 + tps * double(N);
            for (double k = std::ceil(t0 - 1e-9); k < t1; k += 1.0)
                midi.addEvent(&f8, 1, juce::jlimit(0, N - 1, int((k - t0) / tps)));
            m_clockTick = t1;
            m_wasPlaying = playing;
        }
    }

    // MIDI IN: a host sample position -> the source frame it falls on in this render.
    m_midiIn.clear();
    for (const auto meta : midi) {
        const auto* raw = meta.data; const int len = meta.numBytes;
        if (len <= 0) continue;
        if (len > 3) { m_runner->sendMIDIData(raw, size_t(len)); continue; }   // SysEx: at the block start
        const double srcPos = double(meta.samplePosition) * ratio - double(have);
        uint32_t off = srcPos <= 0.0 ? 0u : uint32_t(srcPos);
        if (toRender > 0 && off >= toRender) off = toRender - 1;
        MS2000::Ms2kRunner::MidiEv ev{};
        ev.frame = off; ev.len = uint8_t(len);
        for (int i = 0; i < len; ++i) ev.data[i] = raw[i];
        m_midiIn.push_back(ev);
    }
    midi.clear();

    const uint64_t txBefore = dsp ? dsp->txFrames() : 0;
    m_outBytes.clear();
    if (toRender > 0) {
        static const bool rlog = std::getenv("MS2K_RENDERLOG") != nullptr;   // CPU-PC hunt: render cost per 50 ms
        const double rt0 = rlog ? juce::Time::getMillisecondCounterHiRes() : 0.0;
        m_runner->render(m_tmpL.data(), m_tmpR.data(), toRender, m_midiIn.data(), m_midiIn.size(), m_tmpFlags.data());
        if (rlog) {
            static double sum = 0.0, mx = 0.0, w0 = -1.0;
            const double ms = juce::Time::getMillisecondCounterHiRes() - rt0, t = double(m_frames.load()) / m_hostRate;
            sum += ms; mx = (std::max)(mx, ms);
            if (w0 < 0.0) w0 = t;
            if (t - w0 >= 0.05) {
                static const std::string p = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\ms2k_render.log";
                if (FILE* f = std::fopen(p.c_str(), "a")) { std::fprintf(f, "%8.3f %6.2f %6.2f\n", w0, sum, mx); std::fclose(f); }
                sum = mx = 0.0; w0 = t;
            }
        }
        m_srcL.insert(m_srcL.end(), m_tmpL.begin(), m_tmpL.begin() + toRender);
        m_srcR.insert(m_srcR.end(), m_tmpR.begin(), m_tmpR.begin() + toRender);
    } else {
        for (auto& e : m_midiIn) m_runner->sendMIDIData(e.data, e.len);
    }

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    size_t used = 0;
    if (std::abs(ratio - 1.0) < 1e-9) {
        used = size_t(N);
        std::copy(m_srcL.begin(), m_srcL.begin() + N, outL);
        if (outR) std::copy(m_srcR.begin(), m_srcR.begin() + N, outR);
    } else {
        used = size_t(m_interpL.process(ratio, m_srcL.data(), outL, N));
        if (outR) m_interpR.process(ratio, m_srcR.data(), outR, N);
    }
    if (used > m_srcL.size()) used = m_srcL.size();
    m_srcL.erase(m_srcL.begin(), m_srcL.begin() + ptrdiff_t(used));
    m_srcR.erase(m_srcR.begin(), m_srcR.begin() + ptrdiff_t(used));
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, N);
    m_frames.fetch_add(uint64_t(N), std::memory_order_relaxed);
    const float g = m_gain.load(std::memory_order_relaxed);   // POWER/VOLUME
    if (g != 1.0f) for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch) buffer.applyGain(ch, 0, N, g);

    // MIDI OUT: the firmware's byte stream regrouped into messages (running status, system common,
    // realtime anywhere, SysEx F0..F7 whole), placed at the host position of the frame each byte belongs to.
    auto posOf = [&](uint64_t frame) {
        const double p = double(frame > txBefore ? frame - txBefore : 0) / ratio;
        return juce::jlimit(0, N - 1, int(p));
    };
    // HOSTDIAG-1 (R2): MS2K_HOSTDIAG=1 also logs every MIDI OUT message handed to the host; MS2K_NOMIDIOUT=1
    // keeps them from the host altogether (A/B for the VSTHost Demo -> EXIT freeze: the host stops calling us).
    static const bool s_outDiag = [] { const char* e = std::getenv("MS2K_HOSTDIAG"); return e && *e && *e != '0'; }();
    static const bool s_noOut = [] { const char* e = std::getenv("MS2K_NOMIDIOUT"); return e && *e && *e != '0'; }();
    const bool sysexToHost = sysexOut.load(std::memory_order_relaxed);
    auto emitOut = [&](const uint8_t* d, int len, int at) {
        if (s_outDiag) {
            if (len == 1 && d[0] >= 0xF8) m_diagRt[d[0] - 0xF8].fetch_add(1, std::memory_order_relaxed);
            else {
                const uint32_t w = m_diagOutW.load(std::memory_order_relaxed);
                auto& m = m_diagOut[w & 255];
                m.blk = m_blocks.load(std::memory_order_relaxed); m.len = uint8_t(juce::jmin(len, 255));
                for (int i = 0; i < 7; ++i) m.b[i] = i < len ? d[i] : 0;
                m_diagOutW.store(w + 1, std::memory_order_release);
            }
        }
        if (s_noOut) return;
        // VSTHOST-SYSEX-1 (2026-10-08): Hermann Seib's VSTHost stops calling processBlock for good in the very block
        // in which a VST3 plugin hands it a SysEx output event (measured: blocks frozen at 3130, the block that
        // carried F0 42 30 58 4E 00 00 F7 - the firmware's mode message on Demo -> EXIT; F8/FE before it passed).
        // Cubase takes the same event. A Settings switch (sysexOut, default ON) decides; no host detection.
        if (len > 1 && d[0] == 0xF0 && !sysexToHost) return;
        midi.addEvent(d, len, at);
    };
    for (const auto& ob : m_outBytes) {
        const uint8_t b = ob.b; const int pos = posOf(ob.frame);
        if (b >= 0xF8) { emitOut(&b, 1, pos); continue; }
        if (b == 0xF0) { m_outSysex = true; m_sysex.assign(1, b); m_outStatus = 0; continue; }
        if (m_outSysex) {
            if (b == 0xF7) { m_sysex.push_back(b); emitOut(m_sysex.data(), int(m_sysex.size()), pos); m_outSysex = false; m_sysex.clear(); continue; }
            if (b & 0x80) { m_outSysex = false; m_sysex.clear(); }
            else { m_sysex.push_back(b); continue; }
        }
        if (b & 0x80) {
            m_outStatus = 0; m_outHave = 0;
            if (b < 0xF0) { m_outStatus = b; m_outNeed = ((b & 0xE0) == 0xC0) ? 1 : 2; m_outMsg[0] = b; }
            else if (b == 0xF1 || b == 0xF3) { m_outStatus = b; m_outNeed = 1; m_outMsg[0] = b; }
            else if (b == 0xF2) { m_outStatus = b; m_outNeed = 2; m_outMsg[0] = b; }
            else if (b == 0xF6) emitOut(&b, 1, pos);
            continue;
        }
        if (!m_outStatus) continue;
        m_outMsg[1 + m_outHave++] = b;
        if (m_outHave == m_outNeed) {
            emitOut(m_outMsg, 1 + m_outNeed, pos);
            m_outHave = 0;
            if (m_outStatus >= 0xF0) m_outStatus = 0;
        }
    }
}

// ---- SETTINGS-1: user defaults ----
// Until now the Settings switches lived only in the project chunk, so a new instance (and the restart that
// "DSP on a second core" needs) always came up with the built-in defaults: hostClock + transportMsgs on, the rest off.
static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "MS2000R";
    o.filenameSuffix = "settings";
    o.folderName = "MS2000R";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

void Ms2kProcessor::loadGlobalSettings()
{
    juce::PropertiesFile p(settingsOptions());
    mic2 = p.getBoolValue("mic2", mic2);
    dac20 = p.getBoolValue("dac20", dac20);
    hostClock = p.getBoolValue("hostClock", hostClock.load());
    transportMsgs = p.getBoolValue("transportMsgs", transportMsgs.load());
    knobFollow = p.getBoolValue("knobFollow", knobFollow.load());
    dspThread = p.getBoolValue("dspThread", dspThread.load());
    sysexOut = p.getBoolValue("sysexOut", sysexOut.load());
    powerSwitch = p.getBoolValue("powerSwitch", powerSwitch.load());
    boostDb = juce::jlimit(0, 12, p.getIntValue("boostDb", boostDb.load()));
    editorWDefault = p.getIntValue("editorW", 0);
}

void Ms2kProcessor::saveGlobalSettings() const
{
    juce::PropertiesFile p(settingsOptions());
    p.setValue("mic2", mic2);
    p.setValue("dac20", dac20);
    p.setValue("hostClock", hostClock.load());
    p.setValue("transportMsgs", transportMsgs.load());
    p.setValue("knobFollow", knobFollow.load());
    p.setValue("dspThread", dspThread.load());
    p.setValue("sysexOut", sysexOut.load());
    p.setValue("powerSwitch", powerSwitch.load());
    p.setValue("boostDb", boostDb.load());
    if (editorWDefault >= 700) p.setValue("editorW", editorWDefault);
    p.saveIfNeeded();
}

// ---- plugin state (VST3-2) ----
// A ValueTree "MS2000R": v=1, knobs = 32 positions "a,b,..", in1/in2/volume, mic2/dac20, editorW/editorTab,
// flashMask (sectors SA0..SA18 not factory any more) and flash = those sectors' bytes in SA order, gzipped.
void Ms2kProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree t("MS2000R");
    t.setProperty("v", 1, nullptr);
    juce::String k;
    for (int i = 0; i < 32; ++i) k << (i ? "," : "") << int(knobs[i / 8][i % 8]);
    t.setProperty("knobs", k, nullptr);
    t.setProperty("in1", in1, nullptr); t.setProperty("in2", in2, nullptr); t.setProperty("volume", volume, nullptr);
    t.setProperty("mic2", mic2, nullptr); t.setProperty("dac20", dac20, nullptr);
    t.setProperty("editorW", editorW, nullptr); t.setProperty("editorTab", editorTab, nullptr);
    t.setProperty("hostClock", hostClock.load(), nullptr);
    t.setProperty("transportMsgs", transportMsgs.load(), nullptr);
    t.setProperty("knobFollow", knobFollow.load(), nullptr);
    t.setProperty("dspThread", dspThread.load(), nullptr);
    t.setProperty("sysexOut", sysexOut.load(), nullptr);
    t.setProperty("powerSwitch", powerSwitch.load(), nullptr);
    t.setProperty("boostDb", boostDb.load(), nullptr);
    t.setProperty("libraryPath", libraryPath, nullptr);
    uint32_t mask = 0; const uint8_t* img = nullptr;
    if (m_runner) { auto& f = m_runner->getEmulator().getFlashROM(); mask = f.stateMask(); img = f.data(); }
    else if (m_haveProjectFlash && m_projectMask) { mask = m_projectMask; img = m_projectImage.data(); }
    t.setProperty("flashMask", int(mask), nullptr);
    if (mask && img) {
        juce::MemoryOutputStream raw;
        {
            juce::GZIPCompressorOutputStream gz(raw, 9);
            for (uint32_t sa = 0; sa < MS2000::FlashROM::NUM_SECTORS; ++sa) {
                uint32_t s = 0, e = 0;
                if ((mask & (1u << sa)) && MS2000::FlashROM::sectorRange(sa, s, e)) gz.write(img + s, e - s + 1);
            }
        }
        t.setProperty("flash", raw.getMemoryBlock(), nullptr);
    }
    juce::MemoryOutputStream out(dest, false);
    t.writeToStream(out);
}

void Ms2kProcessor::setStateInformation(const void* data, int size)
{
    const juce::ValueTree t = juce::ValueTree::readFromData(data, size_t(size));
    if (!t.hasType("MS2000R")) return;
    juce::StringArray k; k.addTokens(t.getProperty("knobs").toString(), ",", "");
    for (int i = 0; i < 32 && i < k.size(); ++i) knobs[i / 8][i % 8] = uint16_t(juce::jlimit(0, 1023, k[i].getIntValue()));
    in1 = float(double(t.getProperty("in1", 1.0))); in2 = float(double(t.getProperty("in2", 1.0)));
    volume = float(double(t.getProperty("volume", 1.0)));
    mic2 = bool(t.getProperty("mic2", false)); dac20 = bool(t.getProperty("dac20", false));
    editorW = int(t.getProperty("editorW", 0)); editorTab = int(t.getProperty("editorTab", 0));
    hostClock = bool(t.getProperty("hostClock", true));
    transportMsgs = bool(t.getProperty("transportMsgs", true));
    knobFollow = bool(t.getProperty("knobFollow", false));
    dspThread = bool(t.getProperty("dspThread", false));
    sysexOut = bool(t.getProperty("sysexOut", sysexOut.load()));
    powerSwitch = bool(t.getProperty("powerSwitch", powerSwitch.load()));
    boostDb = juce::jlimit(0, 12, int(t.getProperty("boostDb", boostDb.load())));
    libraryPath = t.getProperty("libraryPath", "").toString();
    syncParams();                                        // AUTOMATION-1: the parameters show this project's panel

    // the flash this project's machine had
    const uint32_t mask = uint32_t(int(t.getProperty("flashMask", 0))) & ((1u << MS2000::FlashROM::NUM_SECTORS) - 1u);
    std::vector<uint8_t> img;
    if (mask) {
        if (const auto* mb = t.getProperty("flash").getBinaryData()) {
            juce::MemoryInputStream mi(*mb, false);
            juce::GZIPDecompressorInputStream gz(mi);
            img.assign(MS2000::FlashROM::FLASH_SIZE, 0xFF);
            bool ok = true;
            for (uint32_t sa = 0; sa < MS2000::FlashROM::NUM_SECTORS && ok; ++sa) {
                uint32_t s = 0, e = 0;
                if ((mask & (1u << sa)) && MS2000::FlashROM::sectorRange(sa, s, e))
                    ok = gz.read(img.data() + s, int(e - s + 1)) == int(e - s + 1);
            }
            if (!ok) { setStatus("The project's flash state is damaged - not loaded."); applyPanel(); return; }
        } else { applyPanel(); return; }
    }
    bool same = false;
    if (m_runner) {
        const auto& f = m_runner->getEmulator().getFlashROM();
        same = f.stateMask() == mask;
        for (uint32_t sa = 0; sa < MS2000::FlashROM::NUM_SECTORS && same; ++sa) {
            uint32_t s = 0, e = 0;
            if ((mask & (1u << sa)) && MS2000::FlashROM::sectorRange(sa, s, e))
                same = std::equal(img.begin() + s, img.begin() + e + 1, f.data() + s);
        }
    }
    m_haveProjectFlash = true; m_projectMask = mask; m_projectImage = std::move(img);
    if (m_runner && !same) {                 // a different machine: power cycle it with this flash
        std::lock_guard<std::mutex> l(m_machineMx);
        stopMachine();
        clearStreams();
        if (!m_poweredOff) startMachine();
    } else {
        applyPanel();
    }
}

juce::AudioProcessorEditor* Ms2kProcessor::createEditor() { return new Ms2kEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ms2kProcessor(); }
