#pragma once
// DSP56362 - IC17 on KOD-A30412 (KORG service manual p.15). BUG105, 2026-09-24.
//
// The real part, built from the upstream dsp56300 library (temp-gearmulator/source/dsp56300,
// d6e4514) and Motorola's own boot ROM - nothing in here computes a value the silicon would not.
//
//   core + peripherals  dsp56k::DSP + dsp56k::Peripherals56362 (SHI, ESAI, DMA, timers, HDI08)
//   memory              dsp56k::Memory(0x1000000): the full 24-bit space, so P:$FF0000 exists
//   boot ROM            full FW/boot-362.ms2000.bin (LOCAL ONLY, never in the repo) - the dump
//                       with the two shi_loop words corrected per KOD-A30412 + UM Appendix A
//                       (CLAUDE.md "VALIDATION"). Override with MS2K_DSPROM=<path>.
//   operating mode      MODD:MODC:MODB:MODA = 0101 = mode 5, read off KOD-A30412 (MODA 10k up,
//                       MODB = HCT08(FS2, PORT_RESET) = 0 in reset, MODC 10k up, MODD 4.7k down)
//   JIT                 dynamicFastInterrupts = true: the stage-1 image runs at P:0..7, the
//                       vector slots, reached by a plain jmp (BUG104, measured both ways)
//   host link           SPI slave on the SHI. The MCU is the master: SCI0 clocked synchronous,
//                       MSB first (SCMR0 = 0xFA, SDIR = 1), SS = PF1 (the firmware's own
//                       BCLR/BSET #1,@PFDR around every DMAC transfer), SCK through a 74HCU04.
//
// STATED APPROXIMATIONS:
//   * the SPI link is modelled per BYTE, at the end of each SCI0 character, not per bit. The
//     inverted SCK is what makes the H8S's clock edges meet the SHI's CPOL/CPHA on the board;
//     at byte level both ends see the same bits, so it is recorded, not simulated.
//   * DSP clock (BUG106): PINIT is tied low (R127 0R), so PEN = 0 out of reset and the core runs
//     from EXTAL = 64FS2 = 12.288 MHz / 4 = 3.072 MHz (74HC4040, KOD-A30412); after Korg's PCTL
//     write the core runs at F_EXTAL x MF / (PDF x DF). The core is paced by the library's own
//     cycle count; its per-instruction cycle figures are the library's, not re-verified here.
//     PLL lock time after a PCTL write is not modelled.
//   * ESAI: slave to the board's clocks - SCKT/SCKR = 64FS1 (3.072 MHz), FST/FSR = FS1 (48 kHz),
//     KOD-A30412. Modelled through the library's EsaiClock at 48 kHz per core-cycle count.
//   * PORT_RESET (the DSP's RESET) is not yet driven by the MCU port that owns it.
#include <atomic>
#include <immintrin.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dsp56k { class DSP; class Memory; class Peripherals56362; class PeripheralsNop;
                   class DefaultMemoryValidator; class MmuHelper; }

namespace MS2000 {
class Ak4522Adc;   // AUDIO-IN, ak4522.h

class DSP56362Emulator {
public:
    DSP56362Emulator();
    ~DSP56362Emulator();

    // Builds the part and loads the boot ROM. False - and says why, once - if the local ROM
    // file is missing: then there is no DSP, which is a gap on the record, not a stand-in.
    bool initialize(uint32_t clockHz = 3072000, uint32_t sampleRate = 48000);
    void reset() {}
    void shutdown() {}
    static constexpr bool isModelled() { return true; }
    bool ready() const { return m_dsp != nullptr; }

    // Advance by the MCU's own phi cycles (10 MHz); converted at the DSP's clock.
    void runForMcuCycles(uint32_t mcuCycles, uint32_t mcuHz);
    // PERF-131: the MCU hands its cycles over here per instruction; the DSP is run only when
    // something can tell the difference - an fs (IRQB) edge falls in the new chunk, or the MCU is
    // about to talk to the DSP (syncMcu() before spiByte / setSS / hreqAsserted). runForMcuCycles()
    // is additive (integer cycle target, and exec() is called while cycles < target), so running
    // the pending chunks in one call executes the same exec() sequence as one call per chunk; the
    // IRQB edge is injected at the same DSP point because the chunk that carries it runs alone,
    // after everything before it. Per-instruction calls were ~3.5 M/s (exec() count, bench).
    // NOT additive across a PCTL change: coreHz() converts each chunk at the clock it finds, and the
    // DSP writes PCTL while it runs (boot: 3.072 MHz -> 99.98 MHz at MCU t=0.92 s). So until the
    // program has switched the PLL on (PCTL PEN, bit 18; DSP56300FM PCTL layout) every chunk is run
    // on its own, exactly as before. STATED: a later PCTL write (none in this firmware) would be
    // converted at batch, not instruction, granularity - neither of which is the silicon.
    void tickMcu(uint32_t mcuCycles, uint32_t mcuHz) {
        m_pendHz = mcuHz;
        static const bool lazyOff = [] { const char* e = std::getenv("MS2K_DSPLAZY"); return e && e[0] == 'o' && e[1] == 'f'; }();   // A/B
        if (!m_batchOk.load(std::memory_order_relaxed) || lazyOff) { post(mcuCycles, mcuHz); return; }
        if (fsPhase() + (uint64_t(m_pendMcu) + mcuCycles) * 48000u >= mcuHz) {
            ++m_fsRuns;
            flushPend();
            post(mcuCycles, mcuHz);
        } else {
            m_pendMcu += mcuCycles;
        }
    }
    // PERF-134: for the MCU's peripheral batching - how many more MCU cycles until the chunk that
    // carries the next fs (IRQB) edge (the chunk c is that chunk iff c >= this), and whether chunks
    // may be merged at all yet.
    bool     mcuBatchOk() const { return m_batchOk.load(std::memory_order_relaxed); }
    uint32_t mcuCyclesToNextFs() const {
        const uint64_t have = fsPhase() + uint64_t(m_pendMcu) * 48000u;
        return have >= m_pendHz ? 0u : uint32_t((m_pendHz - have + 47999u) / 48000u);
    }
    // syncMcu(): the MCU is about to SEE the DSP (SPI byte, SS, HREQ, PORT_RESET, a MIDI OUT stamp): every MCU
    // cycle so far is given to the DSP and - with the DSP thread - waited for.
    void syncMcu() {
        ++m_syncCalls;
        flushPend();
        if (m_thr.joinable()) drain();
    }
    // DSP-THREAD (2026-10-01): the DSP on its own thread. The MCU posts the same (cycles, Hz) chunks it ran
    // before, in the same order, and waits only where it looks at the DSP (syncMcu); runForMcuCycles() runs on
    // the DSP thread and nothing else touches the DSP while it does. The fs phase the MCU batches by is kept on
    // the MCU side (fsPhase: the same arithmetic as runForMcuCycles' m_fsAccum, over the chunks posted).
    void startThread();
    void stopThread();
    bool threaded() const { return m_thr.joinable(); }
    void drain() {
        const uint32_t h = m_qHead.load(std::memory_order_relaxed);
        while (m_qDone.load(std::memory_order_acquire) != h) _mm_pause();
    }
    uint64_t m_syncCalls = 0, m_syncRuns = 0, m_fsRuns = 0;
    void flushPend() { if (m_pendMcu) { ++m_syncRuns; const uint32_t p = m_pendMcu; m_pendMcu = 0; post(p, m_pendHz); } }
    void post(uint32_t mcuCycles, uint32_t mcuHz) {
        if (!m_thr.joinable()) { runForMcuCycles(mcuCycles, mcuHz); return; }
        if (mcuHz) m_fsShadow = (m_fsShadow + uint64_t(mcuCycles) * 48000u) % mcuHz;
        const uint32_t h = m_qHead.load(std::memory_order_relaxed);
        while (h - m_qDone.load(std::memory_order_acquire) >= kQ) _mm_pause();
        m_q[h & (kQ - 1)] = { mcuCycles, mcuHz };
        m_qHead.store(h + 1, std::memory_order_release);
        if (m_thrSleeping.load(std::memory_order_acquire)) { std::lock_guard<std::mutex> l(m_thrMx); m_thrCv.notify_one(); }
    }
    uint64_t fsPhase() const { return m_thr.joinable() ? m_fsShadow : m_fsAccum; }
    struct DspReq { uint32_t cycles, hz; };
    static constexpr uint32_t kQ = 1u << 14;
    DspReq m_q[kQ] = {};
    std::atomic<uint32_t> m_qHead{ 0 }, m_qDone{ 0 };
    std::thread m_thr; std::atomic<bool> m_thrStop{ false }, m_thrSleeping{ false };
    std::mutex m_thrMx; std::condition_variable m_thrCv;
    uint64_t m_fsShadow = 0;   // DSP-THREAD study: how often the MCU must wait for the DSP

    // DSP-RESET (2026-09-27): PORT_RESET = MCU P35 (KOD-A30411, R137 4.7k to GND) -> DSP RESET pin 44
    // (KOD-A30412). Low holds the DSP in reset (no execution, IRQB = FS2 AND PORT_RESET gated off);
    // the rising edge is a hardware reset: MODA-D latched (mode 5), PC = $FF0000, PLL off, peripherals
    // and DMA to their reset state. Memory keeps its contents.
    void     setPortReset(bool high);
    bool     inReset() const { return m_resetHeld; }
    uint64_t resetCount() const { return m_resetReleases; }

    // One SPI byte from the master while SS is asserted (low); returns the MISO byte.
    // With SS deasserted the SHI keeps MISO tri-stated and R249 (10k) pulls it up: 0xFF.
    uint8_t spiByte(uint8_t mosi, bool ssAsserted);
    // SS (PF1) edge, reported by the MCU's PFDR write: true = asserted (low).
    void    setSS(bool asserted);

    // HREQ is an active-low OUTPUT in slave mode (DSP56362/D Table 1-10): true = asserted.
    bool hreqAsserted() const;

    dsp56k::DSP* getGearmulatorDSP() const { return m_dsp.get(); }

    // Host audio (thin GUI): TX0 frames - slot 0 = L, slot 1 = R (BUG128b), 24-bit sign-extended - into a
    // single-producer (emulation thread) / single-consumer (audio device thread) ring. Off by
    // default; when full, new frames are dropped and counted (the host is not keeping up).
    void     enableAudioRing(bool on) { m_audioRingOn = on; }
    bool     popAudio(int32_t& l, int32_t& r) {
        const uint32_t t = m_ringTail.load(std::memory_order_relaxed);
        if (t == m_ringHead.load(std::memory_order_acquire)) return false;
        const uint64_t v = m_ring[t & (kRing - 1)];
        l = int32_t(uint32_t(v)); r = int32_t(uint32_t(v >> 32));
        m_ringTail.store(t + 1, std::memory_order_release);
        return true;
    }
    uint32_t audioFill() const { return m_ringHead.load(std::memory_order_acquire) - m_ringTail.load(std::memory_order_acquire); }
    uint64_t audioDropped() const { return m_ringDropped.load(std::memory_order_relaxed); }

    // AUDIO-IN (2026-09-26): the MS2000 input stage + AK4522 ADC in front of ESAI RX0 (ak4522.h).
    // AIN-SOURCES (2026-09-27, Tamas: each jack needs its own host channel): one mono ring per MS2000
    // input (0 = AUDIO IN 1 -> AINL, 1 = AUDIO IN 2 -> AINR), each fed by whichever capture stream and
    // channel the GUI picked for it; the emulation thread takes one sample of each per RX frame.
    // With nothing pushed an input digitises silence.
    void     pushAudioIn(int input, const float* mono, uint32_t frames) {
        InRing& q = m_in[input & 1];
        for (uint32_t i = 0; i < frames; ++i) {
            const uint32_t h = q.head.load(std::memory_order_relaxed);
            if (h - q.tail.load(std::memory_order_acquire) >= kInRing) { m_inDropped.fetch_add(1, std::memory_order_relaxed); continue; }
            q.ring[h & (kInRing - 1)] = mono[i];
            q.head.store(h + 1, std::memory_order_release);
        }
        q.live.store(true, std::memory_order_release);
    }
    void     stopAudioIn(int input) { m_in[input & 1].live.store(false, std::memory_order_release); }
    void     stopAudioIn() { stopAudioIn(0); stopAudioIn(1); }
    // VR30 / VR31 knob positions 0..1 and SW1 (Input 2 MIC = true).
    void     setInputStage(float vr30, float vr31, bool mic2) { m_vr30.store(vr30); m_vr31.store(vr31); m_mic2.store(mic2); }
    // The AK4522 DAC hears the 20 MSBs of each word (ak4522Dac20); default off = the full 24 bits.
    void     setDac20(bool on) { m_dac20.store(on, std::memory_order_relaxed); }
    uint64_t audioInUnderruns() const { return m_in[0].underrun.load(std::memory_order_relaxed) + m_in[1].underrun.load(std::memory_order_relaxed); }
    bool     audioInLive(int input) const { return m_in[input & 1].live.load(std::memory_order_relaxed); }
    uint64_t audioInClips(int ch) const { return m_inClips[ch & 1].load(std::memory_order_relaxed); }
    uint32_t audioInFill(int input) const { const InRing& q = m_in[input & 1]; return q.head.load(std::memory_order_acquire) - q.tail.load(std::memory_order_acquire); }
    uint32_t audioInFill() const { return audioInLive(0) ? audioInFill(0) : audioInFill(1); }
    static constexpr uint32_t kInRing = 1u << 14;
    static constexpr uint32_t kInTarget = 3840;   // capture clock ahead by more than kInMax (200 ms) -> skip back to 80 ms
    static constexpr uint32_t kInMax = 9600;
    uint64_t audioInSkips() const { return m_in[0].skips.load(std::memory_order_relaxed) + m_in[1].skips.load(std::memory_order_relaxed); }
    uint64_t shiOverruns() const { return m_shiOverruns; }   // GUI log, read across threads (a counter, torn reads harmless on x64)
    static constexpr uint32_t kRing = 1u << 15;

private:
    void pushAudio(int32_t l, int32_t r) {
        const uint32_t h = m_ringHead.load(std::memory_order_relaxed);
        if (h - m_ringTail.load(std::memory_order_acquire) >= kRing) { m_ringDropped.fetch_add(1, std::memory_order_relaxed); return; }
        m_ring[h & (kRing - 1)] = uint64_t(uint32_t(l)) | (uint64_t(uint32_t(r)) << 32);
        m_ringHead.store(h + 1, std::memory_order_release);
    }
    void readAudioIn(uint32_t& wl, uint32_t& wr);   // AUDIO-IN: one RX frame through the ADC model
    void loadAudioInWav(const char* path);          // AUDIO-IN: MS2K_AUDIOIN=<wav>
    std::map<uint32_t, std::pair<uint64_t, uint64_t>> m_loadProf;   // MS2K_DSPLOAD: cycles/instr per exec() start PC
    bool m_loadProfDone = false;
    struct InRing {
        std::atomic<uint32_t> head{0}, tail{0};
        std::atomic<uint64_t> underrun{0}, skips{0};
        std::atomic<bool>     live{false};
        float                 ring[kInRing] = {};
    };
    InRing                m_in[2];
    float readInput(int input);                     // one sample of a live input ring, 0 when idle
    std::atomic<uint64_t> m_inDropped{0};
    std::atomic<uint64_t> m_inClips[2] = {};
    std::atomic<float>    m_vr30{1.0f}, m_vr31{1.0f};
    std::atomic<bool>     m_mic2{false}, m_dac20{false};
    std::vector<float>    m_inFile;                 // MS2K_AUDIOIN=<wav>: headless input, interleaved stereo
    size_t                m_inFilePos = 0;
    std::unique_ptr<Ak4522Adc> m_adc;
    bool                  m_audioRingOn = false;
    std::atomic<uint32_t> m_ringHead{0}, m_ringTail{0};
    std::atomic<uint64_t> m_ringDropped{0};
    uint64_t              m_ring[kRing] = {};
    std::unique_ptr<dsp56k::MmuHelper>              m_mmu;        // BUG119: board memory map (must outlive m_mem)
    std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
    std::unique_ptr<dsp56k::Memory>                 m_mem;
    std::unique_ptr<dsp56k::Peripherals56362>       m_periphX;
    std::unique_ptr<dsp56k::PeripheralsNop>         m_periphY;
    std::unique_ptr<dsp56k::DSP>                    m_dsp;

    uint32_t m_clockHz  = 3072000;
    uint64_t m_accum    = 0;          // mcuCycles * clockHz, carried between calls
    bool     m_ssWas    = false;
    uint32_t m_byteIdx  = 0;
    uint32_t m_inWord   = 0;
    uint32_t m_outWord  = 0;
    uint64_t m_wordsIn  = 0;
    uint64_t m_frameBytes = 0;
    uint32_t m_romEntries = 0;
    uint32_t m_lastPC   = 0;
    uint64_t m_txFrames = 0;          // ESAI TX frames the DSP program has produced
    uint64_t m_txReported = 0;
    uint64_t m_mcuCyclesRun = 0;      // MCU phi cycles this DSP has been advanced by
    uint32_t m_regSeen[5] = {};       // PCTL TCR TCCR RCR RCCR as last reported (BUG106 instrument)
    uint64_t m_nextSummary = 0;
    uint64_t m_cycleTarget = 0;       // BUG106: DSP cycle count the core must reach
    bool     m_resetHeld = true;       // DSP-RESET: PORT_RESET low at power-on (P35 an input, R137 pull-down)
    uint64_t m_resetReleases = 0;
    std::atomic<bool> m_batchOk{ false };   // PERF-131: PLL on (PCTL PEN) - chunks may be batched (set on the DSP thread)
    uint32_t m_pendMcu = 0;           // PERF-131: MCU cycles not yet given to the DSP
    uint32_t m_pendHz  = 10000000;    // PERF-131: the MCU clock of the last tickMcu()
    uint64_t m_fsAccum = 0;           // BUG106: fs (48 kHz) phase against MCU cycles
    uint64_t m_irqbEdges = 0;
    uint32_t m_frameIn[64] = {}, m_frameOut[64] = {}, m_frameWordsN = 0;   // MS2K_SHITRACE (64 words: a whole voice template frame)
    bool     m_outFresh = false;
    unsigned long long m_txNonZero[2][6] = {};   // non-zero TX words, [slot][TX line]
    bool     m_irqbLevelTold = false;
    uint64_t coreHz() const;          // from PCTL (DSP56300FM Table 6-1)
    uint32_t m_instrumentTick = 0;    // PERF-124: runForMcuCycles instrument decimation
    bool m_pb0High = false;           // BUG125: PB0 = IRQD (KOD-A30412)
    uint64_t m_irqdEdges = 0;
    uint64_t m_shiOverruns = 0;       // BUG125: SHI RX words discarded (FIFO full)
    uint64_t m_irqdNs = 0;
    uint64_t m_execCalls = 0;
    // BUG110: {IPL or -1 = disabled, rank within the IPL} of the source behind a vector,
    // from IPRC/IPRP and UM Tables D-2/D-3.
    std::pair<int, int> interruptPriority(uint32_t vba);
    uint64_t m_unknownVectorTold[4] = {};
    std::string m_wavPath;            // MS2K_DSPWAV
    std::vector<int32_t> m_wav;       // interleaved L/R, 24-bit values sign-extended
    void     traceEvents();
public:
    uint64_t txFrames() const { return m_txFrames; }
};

} // namespace MS2000
