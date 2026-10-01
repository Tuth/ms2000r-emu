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
    int editorW = 0, editorTab = 0;                      // the editor's last width and page
    std::atomic<bool> hostClock{ true };                 // MIDI-CLOCK: the host tempo as F8 clocks into MIDI IN
    void setKnob(unsigned mux, unsigned x, uint16_t v);
    void setSwitch(unsigned col, unsigned row, bool down);
    void applyInputStage();
    void applyDac();
    void applyVolume() { m_gain.store(volume, std::memory_order_relaxed); }
    MS2000::SyxTool& syx() { return m_syx; }
    bool chooseHome(const juce::File& dir);     // HOME-1: Settings' folder choice
    juce::String homePath() const { return m_home.getFullPathName(); }   // SYX-1: .syx import / program export through the machine's MIDI

private:
    void timerCallback() override;           // SYX-1: drives m_syx (message thread)
    MS2000::SyxTool m_syx;
    std::atomic<uint64_t> m_frames{ 0 };
    double m_clockTick = 0.0;                // MIDI-CLOCK: where the free-running clock stands (in clocks)      // host frames played since the machine started
    int m_syxDiag = 0;
    bool bootMachine();                      // once, on the first prepareToPlay
    bool startMachine();                     // the power-on itself (boot and reboot)
    void stopMachine();
    void applyPanel();                       // knobs + input stage + DAC into a running machine
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
