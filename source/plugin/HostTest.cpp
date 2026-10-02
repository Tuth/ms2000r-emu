// MS2000R VST3 host test: loads the built .vst3 the way a host does (JUCE VST3PluginFormat), plays a note
// through processBlock at a given host rate and block size, and writes what comes out as a WAV.
// Usage: MS2000R_HostTest <plugin.vst3> <out.wav> [hostRate=44100] [block=512] [seconds=10] [mode]
//   mode "state"  (VST3-2): instance A plays 3 s and gives its state; A is closed; instance B gets that state
//                 BEFORE prepareToPlay (a project being opened), plays, gives its state; the two must agree.
//   mode "editor" (VST3-2): opens the plugin's editor in a window and plays for the given seconds while the
//                 message loop runs (with MS2K_EDITORSHOT set the editor saves its own pixels).
//   mode "knobs0" (VST3-2): the folder's machine with all 32 pots at 0 (phase 1 never set them) - an A/B.
//   mode "noin"   (VST3-2): the input bus disabled - an A/B for the Audio In path.
//   mode "syx"    (SYX-1): plays with the message loop running, so MS2K_SYXIMPORT / MS2K_SYXEXPORT work.
//   mode "clock"  (MIDI-CLOCK): a 140 BPM transport, playing the first half, stopped the second (MS2K_MIDILOG shows the F8s).
//   mode "multi"  (MULTI-1): three instances in this process, played block by block in turn: #0 the usual note
//                 (its WAV is <out.wav>, compare it with a single-instance run), #1 a G4 1.5 s later, #2 no note
//                 (<out>_1.wav, <out>_2.wav). Needs the multi bundle (build_vst/VST3_multi).
//   mode "reboot" (VST3-2): 1 s in, a state with a different flash is set while the machine runs (a power
//                 cycle); the note is played 5 s after that; the WAV / RMS show the machine came back.
#include <JuceHeader.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <iostream>
#include <algorithm>
#include <vector>

namespace {
std::vector<double> g_blockMs;   // PERF-VST: the time of every processBlock call
bool g_pump = false;
int g_note = 60;                 // MULTI-1: the key play() presses
bool g_echo = false;             // SYX-1b: the plugin's MIDI OUT fed back into its MIDI IN (a host loop)
juce::MidiBuffer g_echoBuf;
// MIDI-CLOCK: a host transport - 140 BPM, playing for the first half of the run, stopped for the second
struct TestHead : juce::AudioPlayHead {
    double bpm = 140.0, rate = 44100.0; int64_t pos = 0, startAt = 0, stopAt = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm(bpm); p.setTimeInSamples(pos);
        // plays from startAt to stopAt from ppq 4.0001 (bar 2, as a DAW gives it): FA must go at once
        p.setIsPlaying(pos >= startAt && pos < stopAt); p.setPpqPosition(4.0001 + double(pos - startAt) / rate * bpm / 60.0);
        return p;
    }
};
TestHead* g_head = nullptr;             // SYX-1: run the message loop while playing (the plugin's timers)
std::unique_ptr<juce::AudioPluginInstance> load(juce::VST3PluginFormat& fmt, const juce::PluginDescription& d, double rate, int block)
{
    juce::String err;
    auto inst = fmt.createInstanceFromDescription(d, rate, block, err);
    if (!inst) { std::cerr << "load failed: " << err << "\n"; return {}; }
    inst->enableAllBuses();
    return inst;
}
// renders [from, to) of out, a note-on / note-off at absolute sample positions
void play(juce::AudioPluginInstance& inst, juce::AudioBuffer<float>& out, int from, int to, int block, int noteOn, int noteOff, int& midiOut)
{
    juce::AudioBuffer<float> buf(juce::jmax(2, inst.getTotalNumInputChannels(), inst.getTotalNumOutputChannels()), block);
    for (int pos = from; pos < to; pos += block) {
        const int n = juce::jmin(block, to - pos);
        buf.setSize(buf.getNumChannels(), n, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        if (noteOn >= pos && noteOn < pos + n) midi.addEvent(juce::MidiMessage::noteOn(1, g_note, (juce::uint8)100), noteOn - pos);
        if (noteOff >= pos && noteOff < pos + n) midi.addEvent(juce::MidiMessage::noteOff(1, g_note), noteOff - pos);
        if (g_echo) { for (const auto m : g_echoBuf) midi.addEvent(m.getMessage(), 0); g_echoBuf.clear(); }
        if (g_head) g_head->pos = pos;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        inst.processBlock(buf, midi);
        g_blockMs.push_back(juce::Time::getMillisecondCounterHiRes() - t0);
        if (g_pump && ((pos / block) % 8) == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        for (const auto m : midi) { (void)m; ++midiOut; }
        if (g_echo) g_echoBuf = midi;
        for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, pos, buf, ch, 0, n);
    }
}
// A JUCE host keeps a VST3's state as XML "VST3PluginState" whose "IComponent" child is the plugin's own
// state, base64 (juce_VST3PluginFormat appendStateFrom / createMemoryStreamForState).
juce::ValueTree stateOf(juce::AudioPluginInstance& inst)
{
    juce::MemoryBlock mb; inst.getStateInformation(mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), int(mb.getSize()));
    auto* c = xml ? xml->getChildByName("IComponent") : nullptr;
    juce::MemoryBlock comp;
    if (!c || !comp.fromBase64Encoding(c->getAllSubText())) { std::cerr << "no IComponent state\n"; return {}; }
    return juce::ValueTree::readFromData(comp.getData(), comp.getSize());
}
void setState(juce::AudioPluginInstance& inst, const juce::ValueTree& t)
{
    juce::MemoryOutputStream o; t.writeToStream(o);
    juce::XmlElement x("VST3PluginState");
    x.createNewChildElement("IComponent")->addTextElement(o.getMemoryBlock().toBase64Encoding());
    juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary(x, mb);
    inst.setStateInformation(mb.getData(), int(mb.getSize()));
}
uint64_t fnv(const juce::MemoryBlock& m)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < m.getSize(); ++i) { h ^= uint8_t(m[i]); h *= 1099511628211ull; }
    return h;
}
juce::String flashOf(const juce::ValueTree& t)
{
    const auto* mb = t.getProperty("flash").getBinaryData();
    return "mask " + juce::String::toHexString(int(t.getProperty("flashMask", 0))).toUpperCase() + ", "
         + juce::String(mb ? int(mb->getSize()) : 0) + " bytes gz, fnv " + (mb ? juce::String::toHexString((juce::int64)fnv(*mb)) : juce::String("-"));
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 3) { std::cerr << "usage: MS2000R_HostTest <plugin.vst3> <out.wav> [rate] [block] [seconds] [state|reboot]\n"; return 1; }
    const double rate = argc > 3 ? std::atof(argv[3]) : 44100.0;
    const int block = argc > 4 ? std::atoi(argv[4]) : 512;
    double secs = argc > 5 ? std::atof(argv[5]) : 10.0;
    const juce::String mode = argc > 6 ? argv[6] : "";

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile(descs, juce::String(argv[1]));
    if (descs.isEmpty()) { std::cerr << "no plugin in " << argv[1] << "\n"; return 2; }
    auto inst = load(fmt, *descs[0], rate, block);
    if (!inst) return 3;
    std::cout << "loaded: " << inst->getName() << " in " << inst->getTotalNumInputChannels() << " out " << inst->getTotalNumOutputChannels() << "\n";

    int rc = 0;
    if (mode == "state") {
        inst->prepareToPlay(rate, block);
        juce::AudioBuffer<float> tmp(2, int(3 * rate)); int mo = 0;
        play(*inst, tmp, 0, int(3 * rate), block, -1, -1, mo);
        auto a = stateOf(*inst);
        a.setProperty("knobs", "100,200,300,400,500,600,700,800,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,512,1000", nullptr);
        a.setProperty("mic2", true, nullptr); a.setProperty("editorW", 1234, nullptr);
        std::cout << "A: " << flashOf(a) << "\n";
        inst->releaseResources(); inst.reset();
        inst = load(fmt, *descs[0], rate, block);
        if (!inst) return 3;
        setState(*inst, a);                         // before prepareToPlay: a project being opened
        inst->prepareToPlay(rate, block);
        play(*inst, tmp, 0, int(3 * rate), block, -1, -1, mo);
        const auto b = stateOf(*inst);
        std::cout << "B: " << flashOf(b) << "\n";
        const bool same = flashOf(a) == flashOf(b) && a.getProperty("knobs") == b.getProperty("knobs")
                       && bool(b.getProperty("mic2")) && int(b.getProperty("editorW")) == 1234;
        std::cout << "state round trip: " << (same ? "SAME" : "DIFFERENT") << "\n";
        if (!same) rc = 5;
        secs = 0;
    }

    if (mode == "noin") {     // the input bus disabled: nothing is pushed to AUDIO IN (phase 1 at 44.1 kHz) - an A/B
        auto l = inst->getBusesLayout();
        if (!l.inputBuses.isEmpty()) l.inputBuses.getReference(0) = juce::AudioChannelSet::disabled();
        std::cout << "input bus disabled: " << (inst->setBusesLayout(l) ? "yes" : "NO") << ", in " << inst->getTotalNumInputChannels() << "\n";
    }
    if (mode == "knobs0s") {  // knobs0 in the FIRST instance of the process: the folder's flash state read here
        const juce::File home(juce::SystemStats::getEnvironmentVariable("MS2K_HOME", juce::File::getCurrentWorkingDirectory().getFullPathName()));
        juce::MemoryBlock img; home.getChildFile("ms2000_flash_state.bin").loadFileAsData(img);
        const int mask = home.getChildFile("ms2000_flash_state.sectors").loadFileAsString().trim().getHexValue32();
        juce::MemoryOutputStream raw;
        {
            juce::GZIPCompressorOutputStream gz(raw, 9);
            const uint32_t st[19] = { 0x0, 0x4000, 0x6000, 0x8000, 0x10000, 0x20000, 0x30000, 0x40000, 0x50000, 0x60000, 0x70000, 0x80000, 0x90000, 0xA0000, 0xB0000, 0xC0000, 0xD0000, 0xE0000, 0xF0000 };
            for (int sa = 0; sa < 19; ++sa)
                if (mask & (1 << sa)) gz.write(static_cast<const char*>(img.getData()) + st[sa], (sa < 18 ? st[sa + 1] : 0x100000u) - st[sa]);
        }
        juce::ValueTree t("MS2000R");
        juce::String z; for (int i = 0; i < 32; ++i) z << (i ? ",0" : "0");
        t.setProperty("v", 1, nullptr); t.setProperty("knobs", z, nullptr);
        t.setProperty("flashMask", mask, nullptr); t.setProperty("flash", raw.getMemoryBlock(), nullptr);
        setState(*inst, t);
        std::cout << "first instance, knobs all 0, " << flashOf(t) << "\n";
    }
    if (mode == "knobs0") {   // the same machine (flash) with every pot at 0 instead of the centre: an A/B
        inst->prepareToPlay(rate, block);
        auto t = stateOf(*inst);
        juce::String z; for (int i = 0; i < 32; ++i) z << (i ? ",0" : "0");
        t.setProperty("knobs", z, nullptr);
        inst->releaseResources(); inst.reset();
        inst = load(fmt, *descs[0], rate, block);
        if (!inst) return 3;
        setState(*inst, t);
        std::cout << "knobs all 0, " << flashOf(t) << "\n";
    }

    if (mode == "syx" || mode == "syxecho") g_pump = true;
    TestHead head;
    if (mode == "clock") { head.rate = rate; head.startAt = int64_t(5.0 * rate); head.stopAt = int64_t(secs * rate * 0.75); g_head = &head; inst->setPlayHead(&head); }
    if (mode == "syxecho") g_echo = true;   // SYX-1: MS2K_SYXIMPORT / MS2K_SYXEXPORT act from the plugin's timer
    const int total = int(secs * rate);
    juce::AudioBuffer<float> out(2, juce::jmax(1, total));
    out.clear();
    int midiOutEvents = 0, noteOn = int(4.0 * rate), noteOff = int(7.0 * rate);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (mode == "reboot") {
        inst->prepareToPlay(rate, block);
        const int at = int(1.0 * rate);
        play(*inst, out, 0, at, block, -1, -1, midiOutEvents);
        auto t = stateOf(*inst);
        const int mask = int(t.getProperty("flashMask", 0));
        std::cout << "before: " << flashOf(t) << "\n";
        if (mask) { t.setProperty("flashMask", 0, nullptr); t.removeProperty("flash", nullptr); }
        else {   // factory flash: name sector SA18 with the factory's own bytes - the same machine, another state
            juce::MemoryBlock rom; juce::File::getCurrentWorkingDirectory().getChildFile("flash.bin").loadFileAsData(rom);
            juce::MemoryOutputStream raw;
            { juce::GZIPCompressorOutputStream gz(raw, 9); gz.write(static_cast<const char*>(rom.getData()) + 0xF0000, 0x10000); }
            t.setProperty("flashMask", 1 << 18, nullptr); t.setProperty("flash", raw.getMemoryBlock(), nullptr);
        }
        const auto r0 = juce::Time::getMillisecondCounterHiRes();
        setState(*inst, t);
        std::cout << "power cycle took " << (juce::Time::getMillisecondCounterHiRes() - r0) << " ms; after: " << flashOf(stateOf(*inst)) << "\n";
        noteOn = at + int(5.0 * rate); noteOff = noteOn + int(3.0 * rate);
        play(*inst, out, at, total, block, noteOn, noteOff, midiOutEvents);
    } else if (mode == "editor") {
        inst->prepareToPlay(rate, block);
        std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorIfNeeded());
        if (!ed) { std::cerr << "no editor\n"; return 6; }
        juce::DocumentWindow win("MS2000R editor test", juce::Colours::black, 0);
        win.setUsingNativeTitleBar(true);
        win.setContentNonOwned(ed.get(), true);
        win.setVisible(true);
        const int step = juce::jmax(block, int(rate / 100));   // ~10 ms of audio per message-loop slice
        for (int pos = 0; pos < total; pos += step) {
            play(*inst, out, pos, juce::jmin(total, pos + step), block, noteOn, noteOff, midiOutEvents);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(8);
        }
        win.clearContentComponent(); ed.reset();
    } else if (mode == "multi" || mode == "multied") {
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> more;
        for (int i = 1; i < 3; ++i) { more.push_back(load(fmt, *descs[0], rate, block)); if (!more.back()) return 3; }
        inst->prepareToPlay(rate, block);
        for (auto& m : more) m->prepareToPlay(rate, block);
        std::vector<juce::AudioBuffer<float>> outs(more.size(), juce::AudioBuffer<float>(2, juce::jmax(1, total)));
        for (auto& o : outs) o.clear();
        int mo = 0;
        const int shift = int(1.5 * rate);
        // multied: the first two instances' editors open in windows while they play (two JUCE copies, one message loop)
        std::vector<std::unique_ptr<juce::AudioProcessorEditor>> eds;
        std::vector<std::unique_ptr<juce::DocumentWindow>> wins;
        if (mode == "multied")
            for (auto* p : { inst.get(), more[0].get() }) {
                eds.emplace_back(p->createEditorIfNeeded());
                if (!eds.back()) { std::cerr << "no editor\n"; return 6; }
                wins.push_back(std::make_unique<juce::DocumentWindow>("MS2000R multi " + juce::String(int(wins.size())), juce::Colours::black, 0));
                wins.back()->setUsingNativeTitleBar(true);
                wins.back()->setContentNonOwned(eds.back().get(), true);
                wins.back()->setTopLeftPosition(40 + 60 * int(wins.size()), 40 + 60 * int(wins.size()));
                wins.back()->setVisible(true);
            }
        for (int pos = 0; pos < total; pos += block) {
            const int end = juce::jmin(total, pos + block);
            g_note = 60; play(*inst, out, pos, end, block, noteOn, noteOff, midiOutEvents);
            g_note = 67; play(*more[0], outs[0], pos, end, block, noteOn + shift, noteOff + shift, mo);
            g_note = 60; play(*more[1], outs[1], pos, end, block, -1, -1, mo);
            if (!wins.empty() && ((pos / block) % 8) == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(4);
        }
        for (auto& w : wins) w->clearContentComponent();
        eds.clear(); wins.clear();
        for (size_t i = 0; i < more.size(); ++i) {
            const juce::String name = juce::String(argv[2]).upToLastOccurrenceOf(".", false, false) + "_" + juce::String(int(i) + 1) + ".wav";
            juce::File f(juce::File::getCurrentWorkingDirectory().getChildFile(name));
            f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), rate, 2, 24, {}, 0));
            if (w) w->writeFromAudioSampleBuffer(outs[i], 0, total);
            std::cout << "instance " << (i + 1) << ": ";
            for (int sec = 0; sec < int(secs); ++sec) {
                const float r = outs[i].getRMSLevel(0, int(sec * rate), juce::jmin(int(rate), total - int(sec * rate)));
                std::cout << sec << ":" << (r > 0 ? juce::roundToInt(20.0 * std::log10(r)) : -240) << " ";
            }
            std::cout << "\n";
            more[i]->releaseResources();
        }
        more.clear();
        std::cout << "instance 0: ";
    } else if (total > 0) {
        inst->prepareToPlay(rate, block);   // (knobs0: the state was set above)
        play(*inst, out, 0, total, block, noteOn, noteOff, midiOutEvents);
    }
    const double wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    inst->releaseResources();
    inst.reset();
    if (total <= 0) return rc;

    juce::File f(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), rate, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer(out, 0, total);
    for (int s = 0; s < int(secs); ++s) {   // per-second RMS
        const int a = int(s * rate), n = int(rate);
        const float r = out.getRMSLevel(0, a, juce::jmin(n, total - a));
        std::cout << s << ":" << (r > 0 ? juce::roundToInt(20.0 * std::log10(r)) : -240) << " ";
    }
    if (!g_blockMs.empty()) {   // PERF-VST: per-block cost against the block's own duration
        auto v = g_blockMs; std::sort(v.begin(), v.end());
        const double dur = 1000.0 * block / rate;
        size_t over = 0; for (double x : g_blockMs) over += x > dur;
        auto q = [&](double f) { return v[std::min(v.size() - 1, size_t(f * double(v.size())))]; };
        std::cout << "\nblocks " << v.size() << ", block " << dur << " ms: median " << q(0.5) << " p99 " << q(0.99) << " p99.9 " << q(0.999)
                  << " max " << v.back() << " ms, over the block time " << over;
        size_t overLate = 0; double maxLate = 0; const size_t skip = size_t(3.0 * rate / block);
        for (size_t i = skip; i < g_blockMs.size(); ++i) { overLate += g_blockMs[i] > dur; maxLate = std::max(maxLate, g_blockMs[i]); }
        std::cout << " (after the first 3 s: " << overLate << ", max " << maxLate << " ms)";
    }
#ifdef _WIN32
    {   // DSP-THREAD b: the whole process' CPU time (all threads) against the audio played
        FILETIME c0, e0, k0, u0; GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
        auto ft = [](FILETIME f) { return double((uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime) * 1e-7; };
        std::cout << "\nprocess CPU " << (ft(k0) + ft(u0)) << " s (user " << ft(u0) << ", kernel " << ft(k0) << ") for " << secs << " s of audio";
    }
#endif
    std::cout << "\nmidi out events " << midiOutEvents << ", " << secs << " s at " << rate << " Hz / " << block << " in " << wall << " s wall\n";
    return rc;
}
