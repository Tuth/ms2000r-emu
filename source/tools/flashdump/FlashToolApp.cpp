// FLASHDUMP-1 (2026-10-08): MS2000 Flash Tool - a Windows GUI for unknown-technologies' flash dump (fwpatch + fwdump,
// GPL-3.0) on a real KORG MS2000 / MS2000R: it builds the flash-dump update from KORG's x811v107.sys, installs it
// through the unit's own update mode (the IPL protocol, docs/ipl_protocol.md, ms2k_ipl.cpp) and reads the whole
// 1 MB flash. Everything it sends was run end to end on the emulated machine first (ipl_e2e).
#include <JuceHeader.h>
#include "tools/flashdump/ms2k_ipl.h"
#include <condition_variable>
#include <deque>
#include <mutex>

using namespace ms2kipl;

// ---- MIDI through JUCE ----
class JuceTransport : public Transport, private juce::MidiInputCallback {
public:
    bool open(const juce::String& inId, const juce::String& outId, juce::String& err)
    {
        close();
        m_in = juce::MidiInput::openDevice(inId, this);
        m_out = juce::MidiOutput::openDevice(outId);
        if (!m_in || !m_out) { err = "could not open the MIDI ports (in use by another program?)"; close(); return false; }
        m_in->start();
        return true;
    }
    void close() { if (m_in) m_in->stop(); m_in.reset(); m_out.reset(); std::lock_guard<std::mutex> l(m_mx); m_q.clear(); }
    bool isOpen() const { return m_in && m_out; }
    void flush() { std::lock_guard<std::mutex> l(m_mx); m_q.clear(); }

    bool send(const Bytes& m) override
    {
        if (!m_out || m.size() < 2 || m.front() != 0xF0 || m.back() != 0xF7) return false;
        m_out->sendMessageNow(juce::MidiMessage::createSysExMessage(m.data() + 1, int(m.size() - 2)));
        return true;
    }
    bool receive(Bytes& sx, int timeoutMs) override
    {
        std::unique_lock<std::mutex> l(m_mx);
        if (!m_cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [this] { return !m_q.empty(); })) return false;
        sx = std::move(m_q.front()); m_q.pop_front();
        return true;
    }
    void wait(int ms) override { juce::Thread::sleep(ms); }

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        if (!msg.isSysEx()) return;                       // realtime (the IPL's F8 heartbeat) and the rest: dropped
        Bytes b; b.push_back(0xF0);
        b.insert(b.end(), msg.getSysExData(), msg.getSysExData() + msg.getSysExDataSize());
        b.push_back(0xF7);
        { std::lock_guard<std::mutex> l(m_mx); if (m_q.size() < 64) m_q.push_back(std::move(b)); }
        m_cv.notify_one();
    }
    std::unique_ptr<juce::MidiInput> m_in;
    std::unique_ptr<juce::MidiOutput> m_out;
    std::mutex m_mx; std::condition_variable m_cv; std::deque<Bytes> m_q;
};

// ---- the window ----
class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent()
    {
        auto lbl = [this](juce::Label& l, const juce::String& t, float h = 15.0f) {
            l.setText(t, juce::dontSendNotification); l.setFont(juce::Font(h)); l.setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(l);
        };
        lbl(m_warn, "ONLY for a KORG MS2000 or MS2000R.\n"
                    "NEVER use this with an MS2000B or MS2000BR - other hardware and firmware: it can make the unit unusable.", 17.0f);
        m_warn.setColour(juce::Label::textColourId, juce::Colour(255, 90, 80));
        m_confirm.setButtonText("My unit is an MS2000 or MS2000R (no B in its name) and I accept the risk of a firmware update");
        m_confirm.onClick = [this] { refresh(); };
        addAndMakeVisible(m_confirm);

        lbl(m_midiLbl, "MIDI: the computer's OUT to the unit's MIDI IN, the unit's MIDI OUT to the computer's IN (two separate ports).");
        addAndMakeVisible(m_inBox); addAndMakeVisible(m_outBox);
        m_scan.setButtonText("Refresh ports"); m_scan.onClick = [this] { scanPorts(); }; addAndMakeVisible(m_scan);

        lbl(m_modeLbl, "Update mode: switch the unit off, hold WRITE and the arpeggiator's TYPE key, switch it on. The LCD shows \"IPL s.p.u\".");
        m_check.setButtonText("1. Check the unit"); m_check.onClick = [this] { runCheck(); }; addAndMakeVisible(m_check);

        m_sysBtn.setButtonText("Choose KORG's x811v107.sys..."); m_sysBtn.onClick = [this] { chooseSys(); }; addAndMakeVisible(m_sysBtn);
        lbl(m_sysLbl, "(MS2000 system updater v1.07 from korg.com - not included)");
        m_install.setButtonText("2. Install the flash-dump update"); m_install.onClick = [this] { runInstall(true); }; addAndMakeVisible(m_install);
        m_restore.setButtonText("Restore the original v1.07"); m_restore.onClick = [this] { runInstall(false); }; addAndMakeVisible(m_restore);
        m_dump.setButtonText("3. Read the whole flash (1 MB, about 10 min)"); m_dump.onClick = [this] { runDump(); }; addAndMakeVisible(m_dump);
        m_stop.setButtonText("Stop"); m_stop.onClick = [this] { m_cancel = true; }; addAndMakeVisible(m_stop);

        addAndMakeVisible(m_bar);
        m_log.setMultiLine(true); m_log.setReadOnly(true); m_log.setScrollbarsShown(true);
        m_log.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        addAndMakeVisible(m_log);
        log("MS2000 Flash Tool - flash dump by unknown-technologies (github.com/unknown-technologies/ms2000), GPL-3.0.");
        scanPorts();
        setSize(820, 660);
        refresh();
        startTimerHz(10);
    }
    ~MainComponent() override { m_cancel = true; if (m_worker.joinable()) m_worker.join(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced(14);
        m_warn.setBounds(r.removeFromTop(66)); m_confirm.setBounds(r.removeFromTop(26)); r.removeFromTop(10);
        m_midiLbl.setBounds(r.removeFromTop(20));
        auto row = r.removeFromTop(28);
        m_inBox.setBounds(row.removeFromLeft(300)); row.removeFromLeft(8); m_outBox.setBounds(row.removeFromLeft(300)); row.removeFromLeft(8); m_scan.setBounds(row);
        r.removeFromTop(8); m_modeLbl.setBounds(r.removeFromTop(36));
        m_check.setBounds(r.removeFromTop(30).removeFromLeft(260)); r.removeFromTop(8);
        row = r.removeFromTop(28); m_sysBtn.setBounds(row.removeFromLeft(260)); row.removeFromLeft(8); m_sysLbl.setBounds(row);
        r.removeFromTop(6);
        row = r.removeFromTop(30); m_install.setBounds(row.removeFromLeft(300)); row.removeFromLeft(8); m_restore.setBounds(row.removeFromLeft(220));
        r.removeFromTop(6);
        row = r.removeFromTop(30); m_dump.setBounds(row.removeFromLeft(380)); row.removeFromLeft(8); m_stop.setBounds(row.removeFromLeft(100));
        r.removeFromTop(8); m_bar.setBounds(r.removeFromTop(22)); r.removeFromTop(8);
        m_log.setBounds(r);
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(32, 36, 42)); }

private:
    void timerCallback() override { refresh(); }
    bool busy() const { return m_busy.load(); }
    void refresh()
    {
        const bool ok = m_confirm.getToggleState() && !busy();
        m_inBox.setEnabled(ok); m_outBox.setEnabled(ok); m_scan.setEnabled(ok); m_check.setEnabled(ok);
        m_sysBtn.setEnabled(ok);
        m_install.setEnabled(ok && m_unitOk && !m_patched.empty());
        m_restore.setEnabled(ok && m_unitOk && !m_v107.empty());
        m_dump.setEnabled(ok && m_unitOk);
        m_stop.setEnabled(busy());
    }
    void log(const juce::String& s)
    {
        juce::Component::SafePointer<MainComponent> self(this);
        juce::MessageManager::callAsync([self, s] { if (self) { self->m_log.moveCaretToEnd(); self->m_log.insertTextAtCaret(s + "\n"); } });
    }
    void scanPorts()
    {
        m_ins = juce::MidiInput::getAvailableDevices(); m_outs = juce::MidiOutput::getAvailableDevices();
        m_inBox.clear(); m_outBox.clear();
        for (int i = 0; i < m_ins.size(); ++i) m_inBox.addItem("IN:  " + m_ins[i].name, i + 1);
        for (int i = 0; i < m_outs.size(); ++i) m_outBox.addItem("OUT: " + m_outs[i].name, i + 1);
        if (m_ins.size()) m_inBox.setSelectedId(1, juce::dontSendNotification);
        if (m_outs.size()) m_outBox.setSelectedId(1, juce::dontSendNotification);
        m_unitOk = false;
    }
    bool openPorts()
    {
        const int i = m_inBox.getSelectedId() - 1, o = m_outBox.getSelectedId() - 1;
        if (i < 0 || o < 0) { log("Choose a MIDI IN and a MIDI OUT port."); return false; }
        juce::String err;
        if (!m_midi.open(m_ins[i].identifier, m_outs[o].identifier, err)) { log(err); return false; }
        m_midi.flush();
        return true;
    }
    void start(std::function<void()> job)
    {
        if (busy()) return;
        if (m_worker.joinable()) m_worker.join();
        m_cancel = false; m_busy = true; m_progress = 0.0;
        m_worker = std::thread([this, job] { job(); m_busy = false; });
    }
    ms2kipl::Progress progressFn(bool allowStop)
    {
        return [this, allowStop](int d, int n, const std::string&) {
            m_progress = n ? double(d) / double(n) : 0.0;
            return !(allowStop && m_cancel.load());
        };
    }

    void runCheck()
    {
        if (!openPorts()) return;
        start([this] {
            ms2kipl::Version v; const ms2kipl::Result r = getVersion(m_midi, v);
            if (!r.ok) { log("Check: " + juce::String(r.error)); m_unitOk = false; return; }
            log("Unit in update mode: System " + juce::String(versionText(v.sys)) + ", PCM " + juce::String(versionText(v.pcm))
                + ", User " + juce::String(versionText(v.usr)) + ".");
            if (v.sys != 0x0107) log("Note: the system is not v1.07 - the update mode of other versions was not analysed; v1.07 is what KORG's last updater installs.");
            m_unitOk = true;
        });
    }
    void chooseSys()
    {
        m_chooser = std::make_unique<juce::FileChooser>("KORG MS2000 system file x811v107.sys", juce::File(), "*.sys");
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const juce::File f = fc.getResult();
            if (!f.existsAsFile()) return;
            juce::MemoryBlock mb; f.loadFileAsData(mb);
            const Bytes b(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
            Bytes p; std::string err;
            if (!makeDumpSys(b, p, err)) { m_sysLbl.setText("refused: " + juce::String(err), juce::dontSendNotification); m_v107.clear(); m_patched.clear(); return; }
            m_v107 = b; m_patched = p;
            m_sysLbl.setText("KORG v1.07 verified (SHA-1). Flash-dump version built: " + juce::String(sha1Hex(p)).substring(0, 12), juce::dontSendNotification);
            log("x811v107.sys verified; the flash-dump version differs in " + juce::String(diffCount(b, p)) + " bytes (IPL command 1 + copy count + checksum word).");
        });
    }
    static int diffCount(const Bytes& a, const Bytes& b) { int n = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) n += a[i] != b[i]; return n; }

    void runInstall(bool patched)
    {
        const juce::String what = patched ? "the flash-dump update" : "the original v1.07";
        const bool go = juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Install " + what + "?",
            "This rewrites the unit's system area (256 KB, about 2 minutes).\n\n"
            "- Do NOT switch the unit off and do not unplug MIDI until it says done.\n"
            "- Block 0 (the boot code) is written first; a power cut right then can make the unit unusable.\n"
            "- If anything fails: do NOT switch off - press the install button again (the whole update is repeated).\n\n"
            "Only for MS2000 / MS2000R - never MS2000B / BR.", "Install", "Cancel", this, nullptr);
        if (!go) return;
        const Bytes img = patched ? m_patched : m_v107;
        start([this, img, what] {
            log("Installing " + what + "...");
            const ms2kipl::Result r = install(m_midi, Type::SYS, img, progressFn(true));
            if (r.ok) { m_installed = img; m_unitOk = false; log("Done: " + what + " installed. Switch the unit off and on again in update mode (WRITE + TYPE) to read the flash."); }
            else log("INSTALL FAILED: " + juce::String(r.error));
        });
    }
    void runDump()
    {
        m_saveChooser = std::make_unique<juce::FileChooser>("Save the flash dump", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("flash.bin"), "*.bin");
        m_saveChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            const juce::File f = fc.getResult();
            if (f == juce::File()) return;
            start([this, f] {
                log("Reading 1 MB ...");
                Bytes d;
                const ms2kipl::Result r = dump(m_midi, 0, kFlashSize, d, progressFn(true));
                if (!r.ok) { log("READ FAILED: " + juce::String(r.error)); return; }
                if (!f.replaceWithData(d.data(), d.size())) { log("could not write " + f.getFullPathName()); return; }
                log("Saved " + f.getFullPathName() + "  SHA-1 " + juce::String(sha1Hex(d)));
                const Bytes sys(d.begin(), d.begin() + kSysSize);
                if (isDumpSys(sys)) log("Check: the system area read back is exactly the flash-dump update - the read path is verified.");
                else log("Check: the system area is not the flash-dump update as built here - read it a second time and compare before you rely on it.");
            });
        });
    }

    juce::Label m_warn, m_midiLbl, m_modeLbl, m_sysLbl;
    juce::ToggleButton m_confirm;
    juce::ComboBox m_inBox, m_outBox;
    juce::TextButton m_scan, m_check, m_sysBtn, m_install, m_restore, m_dump, m_stop;
    double m_progress = 0.0;
    juce::ProgressBar m_bar{ m_progress };
    juce::TextEditor m_log;
    juce::Array<juce::MidiDeviceInfo> m_ins, m_outs;
    std::unique_ptr<juce::FileChooser> m_chooser, m_saveChooser;
    JuceTransport m_midi;
    Bytes m_v107, m_patched, m_installed;
    std::atomic<bool> m_busy{ false }, m_cancel{ false }, m_unitOk{ false };
    std::thread m_worker;
};

class FlashToolApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "MS2000 Flash Tool"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& cmd) override
    {
        if (cmd.contains("--test")) { runTest(cmd); quit(); return; }
        if (cmd.contains("--shot")) {   // the window's own pixels, for a look without a screen session
            MainComponent c; juce::StringArray a; a.addTokens(cmd, " ", "\"");
            const juce::File f(a[a.indexOf("--shot") + 1].unquoted());
            f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat().writeImageToStream(c.createComponentSnapshot(c.getLocalBounds()), os);
            quit(); return;
        }
        m_win = std::make_unique<Window>();
    }
    // Test path without the window (FLASHDUMP-1): the same transport and session code the buttons use.
    //   --test --in <port> --out <port> --log <file> [--sys <x811v107.sys> --install] [--dump <file>]
    static void runTest(const juce::String& cmd)
    {
        juce::StringArray a; a.addTokens(cmd, " ", "\"");
        auto arg = [&](const char* k) { const int i = a.indexOf(k); return i >= 0 && i + 1 < a.size() ? a[i + 1].unquoted() : juce::String(); };
        juce::FileOutputStream logf(juce::File(arg("--log")));
        auto log = [&](const juce::String& s) { logf << s << "\n"; logf.flush(); };
        juce::String inId, outId, err;
        for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == arg("--in")) inId = d.identifier;
        for (auto& d : juce::MidiOutput::getAvailableDevices()) if (d.name == arg("--out")) outId = d.identifier;
        JuceTransport t;
        if (!t.open(inId, outId, err)) { log("ports: " + err); return; }
        ms2kipl::Version v; ms2kipl::Result r = getVersion(t, v);
        log("version: " + juce::String(r.ok ? "S " + versionText(v.sys) + " P " + versionText(v.pcm) + " U " + versionText(v.usr) : r.error));
        if (!r.ok) return;
        if (a.contains("--install")) {
            juce::MemoryBlock mb; juce::File(arg("--sys")).loadFileAsData(mb);
            const Bytes b(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
            Bytes p; std::string e;
            if (!makeDumpSys(b, p, e)) { log("sys: " + juce::String(e)); return; }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            r = install(t, Type::SYS, p, [&](int d, int n, const std::string&) { if (d % 8 == 0) log("  block " + juce::String(d) + "/" + juce::String(n)); return true; });
            log("install: " + juce::String(r.ok ? "OK" : r.error) + juce::String::formatted("  (%.1f s)", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0));
        }
        if (arg("--dump").isNotEmpty()) {
            Bytes d; const auto t0 = juce::Time::getMillisecondCounterHiRes();
            r = dump(t, 0, kFlashSize, d, [&](int k, int n, const std::string&) { if (k % 1024 == 0) log("  read " + juce::String(k) + "/" + juce::String(n)); return true; });
            log("dump: " + juce::String(r.ok ? "OK" : r.error) + juce::String::formatted("  (%.1f s)", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0));
            if (r.ok) {
                juce::File(arg("--dump")).replaceWithData(d.data(), d.size());
                log("sha1 " + juce::String(sha1Hex(d)) + (isDumpSys(Bytes(d.begin(), d.begin() + kSysSize)) ? "  system area = the flash-dump update" : "  system area is NOT the flash-dump update"));
            }
        }
        t.close();
    }
    void shutdown() override { m_win.reset(); }
private:
    struct Window : juce::DocumentWindow {
        Window() : juce::DocumentWindow("MS2000 Flash Tool (MS2000 / MS2000R only)", juce::Colour(32, 36, 42), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true); centreWithSize(getWidth(), getHeight()); setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> m_win;
};

START_JUCE_APPLICATION(FlashToolApp)
