// FLASHDUMP-1 (2026-10-08): MS2000 IPL bridge - the EMULATED MS2000R on two Windows MIDI ports, so the Flash Tool can be
// tried end to end without a real unit (e.g. with loopMIDI: two SEPARATE ports, one per direction, else it feeds back).
//   MS2000IplBridge --image <work.bin> --in <port the tool sends to> --out <port the tool reads> [--normal] [--secs N]
// The machine boots from <work.bin> (made from flash.bin on the first run) with WRITE + TYPE held = update mode, unless
// --normal; every flash change is written back to <work.bin> (the runner's whole-chip image, DEV-FLASH-1), so a second
// run is the "power cycle" that starts the freshly installed IPL. flash.bin itself is never written.
#include <JuceHeader.h>
#include "core/ms2000_runner.h"
#include "core/h8s2350_emulator.h"
#include "core/lcd_gui.h"
#include <deque>
#include <mutex>

class Bridge : private juce::MidiInputCallback {
public:
    std::mutex mx; std::deque<uint8_t> toMachine;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    {
        std::lock_guard<std::mutex> l(mx);
        toMachine.insert(toMachine.end(), m.getRawData(), m.getRawData() + m.getRawDataSize());
    }
    juce::MidiInputCallback* cb() { return this; }
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::String image, inName, outName; bool normal = false; double secs = 1e9;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        if (a == "--image" && i + 1 < argc) image = argv[++i];
        else if (a == "--in" && i + 1 < argc) inName = argv[++i];
        else if (a == "--out" && i + 1 < argc) outName = argv[++i];
        else if (a == "--normal") normal = true;
        else if (a == "--secs" && i + 1 < argc) secs = std::atof(argv[++i]);
    }
    if (image.isEmpty() || inName.isEmpty() || outName.isEmpty()) {
        std::printf("usage: MS2000IplBridge --image <work.bin> --in <MIDI in port> --out <MIDI out port> [--normal] [--secs N]\n");
        for (auto& d : juce::MidiInput::getAvailableDevices()) std::printf("  in:  %s\n", d.name.toRawUTF8());
        for (auto& d : juce::MidiOutput::getAvailableDevices()) std::printf("  out: %s\n", d.name.toRawUTF8());
        return 2;
    }
    const juce::File work = juce::File::getCurrentWorkingDirectory().getChildFile(image);
    if (work.getFileName().equalsIgnoreCase("flash.bin")) { std::printf("refused: give a work file, not flash.bin itself\n"); return 2; }
    Bridge br;
    std::unique_ptr<juce::MidiInput> in; std::unique_ptr<juce::MidiOutput> out;
    for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == inName) in = juce::MidiInput::openDevice(d.identifier, br.cb());
    for (auto& d : juce::MidiOutput::getAvailableDevices()) if (d.name == outName) out = juce::MidiOutput::openDevice(d.identifier);
    if (!in || !out) { std::printf("cannot open MIDI in '%s' / out '%s'\n", inName.toRawUTF8(), outName.toRawUTF8()); return 3; }

    MS2000::Ms2kConfig cfg;
    cfg.romPath = "flash.bin"; cfg.cpuCyclesPerTick = 20000; cfg.sampleRate = 48000; cfg.quietBoot = true;
    cfg.useMmcss = false; cfg.audio.enabled = false; cfg.flashStateLoad = false; cfg.flashStateSave = false;
    cfg.devFlashPath = work.getFullPathName().toStdString();      // loaded if it exists, else made from flash.bin; kept up to date
    MS2000::Ms2kGuiHooks hooks; hooks.logFn = [](const char*) {};
    MS2000::Ms2kRunner r(cfg, hooks);
    if (!r.init() || !r.startManual()) { std::printf("machine did not start\n"); return 4; }
    std::vector<uint8_t> outBuf; std::mutex outMx;
    r.getEmulator().setMidiOutSink([&](uint8_t b) { std::lock_guard<std::mutex> l(outMx); outBuf.push_back(b); });
    if (!normal) { r.getEmulator().setPanelSwitch(2, 1, true); r.getEmulator().setPanelSwitch(4, 1, true); }   // TYPE + WRITE
    in->start();
    std::printf("MS2000R (emulated) on MIDI in '%s' / out '%s', %s, image %s\n", inName.toRawUTF8(), outName.toRawUTF8(),
                normal ? "normal power-on" : "UPDATE MODE (WRITE + TYPE held)", work.getFullPathName().toRawUTF8());
    std::fflush(stdout);

    // real time: one 1 ms machine tick per wall millisecond; outgoing bytes are grouped into whole messages
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    std::vector<uint8_t> sx; std::string lastLcd;
    for (uint64_t ms = 0; ms < uint64_t(secs * 1000.0); ++ms) {
        { std::lock_guard<std::mutex> l(br.mx); if (!br.toMachine.empty()) { std::vector<uint8_t> v(br.toMachine.begin(), br.toMachine.end()); br.toMachine.clear(); r.sendMIDIData(v.data(), v.size()); } }
        r.tick1ms();
        if (ms == 2000 && !normal) { r.getEmulator().setPanelSwitch(2, 1, false); r.getEmulator().setPanelSwitch(4, 1, false); }   // keys let go
        std::vector<uint8_t> o; { std::lock_guard<std::mutex> l(outMx); o.swap(outBuf); }
        for (uint8_t b : o) {
            if (b >= 0xF8) continue;                                   // realtime (the IPL's F8 heartbeat): not forwarded
            if (b == 0xF0) sx.assign(1, b);
            else if (!sx.empty()) { sx.push_back(b); if (b == 0xF7) { out->sendMessageNow(juce::MidiMessage(sx.data(), int(sx.size()))); sx.clear(); } }
            else if (b < 0xF0) { /* running status traffic in normal mode: not needed here */ }
        }
        if (ms % 500 == 0) {
            const LcdGuiSnapshot s = r.getLcdGuiSnapshot();
            const std::string lcd = std::string(s.line0) + " | " + s.line1;
            if (lcd != lastLcd) { std::printf("[%7.1f s] LCD %s\n", ms / 1000.0, lcd.c_str()); std::fflush(stdout); lastLcd = lcd; }
        }
        const double ahead = (t0 + double(ms + 1)) - juce::Time::getMillisecondCounterHiRes();
        if (ahead > 1.0) juce::Thread::sleep(int(ahead));
    }
    in->stop();
    r.stop();   // DEV-FLASH-1: the final image save
    return 0;
}
