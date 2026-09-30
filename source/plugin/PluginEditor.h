// MS2000R VST3 editor (phase 2, VST3-2): the vector front panel (gui/vector_panel.h - the same layout the
// standalone draws) on a JUCE Graphics backend, and a Settings page for what sits on the rear panel or inside.
#pragma once
#include <JuceHeader.h>
#include <string>
#include "PluginProcessor.h"
#include "core/lcd_gui.h"
#include "gui/vector_panel.h"

class Ms2kEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Ms2kEditor(Ms2kProcessor& p);
    ~Ms2kEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    // the mouse as the panel sees it (read by the backends)
    struct Mouse {
        VPanel::V2 pos; bool inside = false, btn = false, press = false, dbl = false, shift = false;
        float dragY = 0.0f, wheel = 0.0f, lastY = 0.0f;
        std::string active;                 // the item the button went down on
    };

private:
    void timerCallback() override;
    void runInput();                        // the panel's input pass (no drawing)
    void showTab(int t);
    juce::Rectangle<float> panelArea() const;
    void drawLcd(juce::Graphics& g, juce::Rectangle<float> r) const;
    void setupIo();

    Ms2kProcessor& m_proc;
    VPanel::IO m_io;
    float m_lit[8][12] = {};
    bool m_shown[8][12] = {};
    Mouse m_mouse;
    LcdGuiSnapshot m_lcd{};
    uint8_t m_cg[256][8] = {}; bool m_cgOk = false;
    juce::Typeface::Ptr m_face;
    juce::Image m_static;                   // the printed panel (VPanel layer false), at the display's pixel scale
    bool m_dirty = true;
    int m_tab = 0;
    int m_demoPhase = 0; juce::uint32 m_demoT0 = 0;
    double m_paintMs = 0.0;
    // R2 diagnostics, default OFF: MS2K_EDITORSHOT=<file.png> writes this editor's own pixels (never the
    // screen) 3 s after it opens; MS2K_EDITORTEST=latch first Shift+clicks pads 1 and 5 and EXIT through
    // the same input pass the mouse uses.
    int m_testTick = -1;
    juce::String m_shotPath, m_testMode;
    void testClick(float px, float py, bool shift);

    juce::TextButton m_tabPanel{ "Panel" }, m_tabSettings{ "Settings" };
    juce::ToggleButton m_mic2{ "AUDIO IN 2 = MIC  (rear panel switch SW1; off = LINE)" };
    juce::ToggleButton m_dac20{ "DAC 20-bit  (the AK4522 hears the 20 MSBs; off = all 24 bits)" };
    juce::TextButton m_demo{ "Demo songs  (EXIT + GLOBAL)" };
    juce::Label m_help, m_status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ms2kEditor)
};
