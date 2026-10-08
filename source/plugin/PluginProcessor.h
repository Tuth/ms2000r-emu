// MS2000R VST3 (phase 1, 2026-09-28): the emulator as a plugin around Ms2kRunner::render() (ENGINE-BLOCK).
// Phase 2 (VST3-2): the panel's state (knob positions, input stage, DAC option, volume) and the machine's
// non-volatile flash live in the host project; the editor draws the vector panel (vector_panel.h).
#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include "core/ms2000_runner.h"
#include "core/syx_tool.h"
#include "panel_params.h"
#include "core/master_vr.h"
#include <array>

class Ms2kProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    Ms2kProcessor();
    ~Ms2kProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MS2000R"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- for the editor (message thread) ----
    MS2000::Ms2kRunner* runner() { return m_runner.get(); }
    juce::String status() const { std::lock_guard<std::mutex> l(m_statusMx); return m_status; }
    // The panel as the machine sees it. Knob positions 0..1023 ([mux][x]); AUDIO IN 1/2 level pots (VR30/VR31)
    // and SW1 (Input 2 MIC/LINE); the AK4522 DAC 20-bit option; POWER/VOLUME = the plugin's output gain.
    uint16_t knobs[4][8];
    float in1 = 1.0f, in2 = 1.0f, volume = 1.0f;
    bool mic2 = false, dac20 = false;
    int editorWDefault = 0;                              // UI-SIZE-1: the last editor width, a user default (settings file)
    int editorW = 0, editorTab = 0;                      // the editor's last width and page
    std::atomic<bool> hostClock{ true };                 // MIDI-CLOCK: the host tempo as F8 clocks into MIDI IN
    std::atomic<bool> dspThread{ false };                // DSP-THREAD: the DSP on a second core (from the next start)
    std::atomic<bool> knobFollow{ false };               // KNOB-FOLLOW: the pots show the program (off = where you left them)
    std::atomic<bool> sysexOut{ true };                  // VSTHOST-SYSEX-1: hand the firmware's SysEx OUT to the host
    std::atomic<bool> powerSwitch{ true };               // PWR-SW-1: the Master VR's switch - POWER off at the minimum
    std::atomic<int> boostDb{ 0 };                       // OUT-BOOST-1: master output boost after the pot, 0..12 dB
    std::atomic<bool> transportMsgs{ true };             // MIDI-CLOCK b: Start (on a quarter) / Stop with the host transport
    juce::String libraryPath;                            // LIBRARY-1: the .syx the Library page shows
    void setKnob(unsigned mux, unsigned x, uint16_t v);
    void setSwitch(unsigned col, unsigned row, bool down);
    void applyInputStage();
    void applyDac();
    // MVR-1: Master VR = ALPS RK0971221Z05 (KOD-A30413; parts list p.23 "W/SW", no value), 10 kOhm LINEAR,
    // 2 sections, with switch (distributor data sheet, 2026-10-08). Its wiper drives C134/C117 into R158/R135
    // 22k (at audio frequencies the 10uF is a short), so the divider is loaded:
    //   g(x) = x*RL / (RL + x*(1-x)*R),  R = 10k, RL = 22k:  g(1) = 1, g(0.5) = -6.96 dB, g(0.25) = -12.75 dB.
    // PWR-SW-1: its switch section (PSW1/PSW2) breaks the power at the minimum - see powerSwitchPoll().
    static float masterVrGain(float x) { return ms2kMasterVrGain(x); }   // core/master_vr.h (shared with the standalone)
    void applyVolume() { m_gain.store(masterVrGain(volume) * ms2kBoostGain(boostDb.load()), std::memory_order_relaxed); }
    MS2000::SyxTool& syx() { return m_syx; }
    // SETTINGS-1: the Settings page's switches are also the user's defaults for every NEW instance
    // (%APPDATA%\MS2000R\MS2000R.settings). A project's own state still overrides them when it is loaded.
    void loadGlobalSettings();
    void saveGlobalSettings() const;
    bool chooseHome(const juce::File& dir);     // HOME-1: Settings' folder choice
    juce::String homePath() const { return m_home.getFullPathName(); }
    // AUTOMATION-1 (panel_params.h): the editor reports what the hand did; the parameter follows it, inside a host
    // gesture that endUserEdits() closes (mouse up). Nothing else moves a parameter.
    void userKnob(unsigned mux, unsigned x, uint16_t v);
    void userKey(unsigned col, unsigned row, bool down);
    void userVolume();
    void userInputs();                       // in1, in2, mic2
    void endUserEdits();
    bool poweredOff() const { return m_poweredOff; }   // PWR-SW-1: switched off by the Master VR (message thread)   // SYX-1: .syx import / program export through the machine's MIDI

private:
    void timerCallback() override;           // SYX-1: drives m_syx (message thread)
    MS2000::SyxTool m_syx;
    std::atomic<uint64_t> m_frames{ 0 };
    std::atomic<uint64_t> m_blocks{ 0 };                 // HOSTDIAG-1: processBlock calls (incl. the silent ones)
    int m_hostDiagTick = 0;
    // HOSTDIAG-1: MIDI OUT seen by the host, recorded on the audio thread, printed by the timer (no OutputDebugString
    // on the audio thread - the first version did that per message and flooded the machine).
    struct DiagMsg { uint64_t blk; uint8_t len; uint8_t b[7]; };
    std::array<DiagMsg, 256> m_diagOut{};
    std::atomic<uint32_t> m_diagOutW{ 0 };
    uint32_t m_diagOutR = 0;
    std::array<std::atomic<uint64_t>, 8> m_diagRt{};      // F8..FF counts
    double m_clockTick = 0.0;                // MIDI-CLOCK: where the free-running clock stands (in clocks)
    bool m_wasPlaying = false, m_startPending = false;      // host frames played since the machine started
    int m_syxDiag = 0;
    int m_demoDiag = 0;   // DEMO-FMT-1 diag: MS2K_DEMOAT
    long m_stormN = -1; int m_stormPad = -1; bool m_stormDown = false; double m_stormWall = 0.0;   // PADSTORM diag
    int m_ramDumps = 0;   // DEMO-FMT-1 diag: MS2K_RAMDUMPAT
    bool bootMachine();                      // once, on the first prepareToPlay
    bool startMachine();                     // the power-on itself (boot and reboot)
    void stopMachine();
    void applyPanel();                       // knobs + input stage + DAC into a running machine
    void clearStreams();                     // the audio/MIDI queues of a stopped machine
    // PWR-SW-1: the Master VR at its minimum switches the machine off (flash kept, as on the unit), turning it up
    // powers it on again - with the keys the panel holds (LATCH) down during the boot. Settings can disable it.
    bool powerSwitchOpen() const { return powerSwitch.load() && volume <= 0.001f; }
    void powerSwitchPoll();                  // message thread (timer)
    bool m_poweredOff = false;
    bool m_swHeld[8][8] = {};                // what the panel holds down - set again into a fresh machine
    // AUTOMATION-1
    std::array<juce::RangedAudioParameter*, ms2kparams::kCount> m_params{};
    std::array<std::atomic<float>, ms2kparams::kCount> m_applied{};   // the value the panel has from each parameter
    std::array<bool, ms2kparams::kCount> m_gesture{};
    bool m_anyGesture = false;
    int m_knobParam[4][8], m_keyParam[8][8];
    float panelValue(int i) const;           // the panel's control i, 0..1
    void syncParams();                       // parameters := the panel (construction, project load)
    void pollParams(bool live);              // audio thread: a changed parameter moves its control
    void userParam(int i, float v);
    void setStatus(const juce::String& s) { std::lock_guard<std::mutex> l(m_statusMx); m_status = s; }

    std::unique_ptr<MS2000::Ms2kRunner> m_runner;
    std::mutex m_machineMx;                  // held by a reboot; processBlock only try-locks it
    bool m_bootTried = false, m_owner = false;
    juce::File m_home;
    double m_hostRate = 48000.0;
    std::atomic<float> m_gain{ 1.0f };

    // flash state from the project, for the next power-on (VST3-2)
    bool m_haveProjectFlash = false;
    uint32_t m_projectMask = 0;
    std::vector<uint8_t> m_projectImage;

    // 48 kHz (the DSP's fs) -> host rate
    std::vector<float> m_srcL, m_srcR;      // rendered, not yet consumed source frames
    std::vector<float> m_tmpL, m_tmpR;
    std::vector<uint8_t> m_tmpFlags;
    juce::LagrangeInterpolator m_interpL, m_interpR;
    std::vector<MS2000::Ms2kRunner::MidiEv> m_midiIn;
    // host rate -> 48 kHz for Audio In (VST3-2)
    std::vector<float> m_inFifo[2], m_inTmp;
    juce::LagrangeInterpolator m_inInterp[2];

    // MIDI OUT: bytes from the firmware's TDR1, stamped with the DSP frame they belong to
    struct OutByte { uint64_t frame; uint8_t b; };
    std::vector<OutByte> m_outBytes;
    uint8_t m_outStatus = 0, m_outMsg[3] = {}; int m_outHave = 0, m_outNeed = 0;
    bool m_outSysex = false; std::vector<uint8_t> m_sysex;

    mutable std::mutex m_statusMx;
    juce::String m_status = "starting";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ms2kProcessor)
};
