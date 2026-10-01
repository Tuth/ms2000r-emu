// DSP56362 - the real part from the upstream library + Motorola's boot ROM. See the header.
#include "dsp56362_emulator.h"
#include "ak4522.h"
#include <fstream>

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/interrupts.h"
#include "dsp56kBase/mmuhelper.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <map>
#include <algorithm>
#include "crash_handlers.h"

// VOCODER-1 (2026-09-26, R2 diagnostic, default off): MS2K_DSPRING=1 keeps the last exec() starts (PC, SR, SP,
// LC, cycle count) and prints them if the process dies of a stack overflow (crash_handlers hook).
namespace {
struct DspRingEntry { uint32_t pc, sr, sp, lc, mode, fifo; unsigned long long cyc; };
constexpr unsigned kDspRingN = 8192;
DspRingEntry g_dspRing[kDspRingN]; unsigned g_dspRingI = 0; bool g_dspRingOn = false;
const dsp56k::SHI* g_ringShi = nullptr; const unsigned long long* g_ringWordsIn = nullptr; const unsigned long long* g_ringOverruns = nullptr;
void dumpDspRing()
{
    printf("[DSP-RING] last %u exec() starts, oldest first: PC SR SP LC mode cycles\n", kDspRingN);
    for (unsigned k = 0; k < kDspRingN; ++k) { const auto& e = g_dspRing[(g_dspRingI + k) % kDspRingN];
        printf("[DSP-RING] P:%06X SR=%06X SP=%02X LC=%06X m=%u fifo=%u c=%llu\n", e.pc, e.sr, e.sp, e.lc, e.mode, e.fifo, e.cyc); }
    if (g_ringShi) printf("[DSP-RING] SHI: words in %llu, overruns %llu, HRX reads %llu, empty-FIFO reads %llu, in FIFO %u, deepest %u\n",
        g_ringWordsIn ? *g_ringWordsIn : 0ull, g_ringOverruns ? *g_ringOverruns : 0ull, (unsigned long long)g_ringShi->hrxReads(),
        (unsigned long long)g_ringShi->hrxEmptyReads(), g_ringShi->rxCount(), g_ringShi->rxMaxFill());
    fflush(stdout);
}
}

namespace MS2000 {

using namespace dsp56k;

DSP56362Emulator::DSP56362Emulator() = default;
// AUDIO-IN: one RX frame. Source: the MS2K_AUDIOIN file first (headless measurement), then the host
// capture ring.
// The capture device and the output device run on two clocks, and the emulation runs ahead of the wall clock
// by the output ring in bursts. An empty ring while live = an underrun: silence for that frame, and the reader
// falls one frame behind, so the ring settles at the fill the bursts need (self-priming, no fixed latency).
// If capture gets more than 200 ms ahead, the oldest frames are skipped down to 80 ms (counted as skips).
// (2026-09-27: the "wavering" Tamas heard was the Audio In oscillator's Control2 = LFO1 depth on the
// IN1/IN2 balance - owner's manual p.38 - not this ring.)
float DSP56362Emulator::readInput(int input)
{
    InRing& q = m_in[input & 1];
    if (!q.live.load(std::memory_order_acquire)) return 0.0f;
    uint32_t t = q.tail.load(std::memory_order_relaxed);
    const uint32_t h = q.head.load(std::memory_order_acquire);
    if (h - t > kInMax) { t = h - kInTarget; q.skips.fetch_add(1, std::memory_order_relaxed); }
    float v = 0.0f;
    if (t != h) { v = q.ring[t & (kInRing - 1)]; ++t; }
    else q.underrun.fetch_add(1, std::memory_order_relaxed);
    q.tail.store(t, std::memory_order_release);
    return v;
}

void DSP56362Emulator::readAudioIn(uint32_t& wl, uint32_t& wr)
{
    float l = 0.0f, r = 0.0f;
    if (m_inFilePos + 1 < m_inFile.size()) {
        l = m_inFile[m_inFilePos]; r = m_inFile[m_inFilePos + 1]; m_inFilePos += 2;
    } else {
        l = readInput(0); r = readInput(1);
    }
    if (!std::isfinite(l)) l = 0.0f;   // a non-finite sample would stay in the ADC's HPF state for ever
    if (!std::isfinite(r)) r = 0.0f;
    const double vl = double(l) * Ms2kInputStage::kHostFullScaleV * Ms2kInputStage::kGainIn1 *
                      ms2kAudioTaper(m_vr30.load(std::memory_order_relaxed));
    const double vr = double(r) * Ms2kInputStage::kHostFullScaleV *
                      (m_mic2.load(std::memory_order_relaxed) ? Ms2kInputStage::kGainIn2Mic : Ms2kInputStage::kGainIn2Line) *
                      ms2kAudioTaper(m_vr31.load(std::memory_order_relaxed));
    bool cl = false, cr = false;
    m_adc->process(vl, vr, wl, wr, cl, cr);
    if (cl) m_inClips[0].fetch_add(1, std::memory_order_relaxed);
    if (cr) m_inClips[1].fetch_add(1, std::memory_order_relaxed);
}

// MS2K_AUDIOIN=<file.wav>: RIFF PCM 16/24/32-bit or IEEE float 32, mono or stereo, played into the
// inputs from the DSP's first RX frame on (mono feeds both). A rate other than 48 kHz is used as is
// and reported - no resampling here.
void DSP56362Emulator::loadAudioInWav(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8; };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) {
        printf("[AUDIO-IN] %s: not a RIFF/WAVE file - input stays silent\n", path); return;
    }
    uint32_t fmt = 0, ch = 0, rate = 0, bits = 0; size_t data = 0, len = 0;
    for (size_t o = 12; o + 8 <= b.size(); ) {
        const uint32_t sz = u32(o + 4);
        if (!std::memcmp(b.data() + o, "fmt ", 4) && o + 24 <= b.size()) {
            fmt = u16(o + 8); ch = u16(o + 10); rate = u32(o + 12); bits = u16(o + 22);
            if (fmt == 0xFFFE && o + 34 <= b.size()) fmt = u16(o + 32);   // WAVE_FORMAT_EXTENSIBLE: sub-format
        } else if (!std::memcmp(b.data() + o, "data", 4)) { data = o + 8; len = std::min<size_t>(sz, b.size() - data); break; }
        o += 8 + sz + (sz & 1);
    }
    if (!data || !ch || !(fmt == 1 || fmt == 3)) { printf("[AUDIO-IN] %s: unsupported format %u - input stays silent\n", path, fmt); return; }
    const size_t bps = bits / 8, frames = len / (bps * ch);
    m_inFile.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < 2; ++c) {
            const size_t o = data + (i * ch + (c < ch ? c : 0)) * bps;
            float v = 0.0f;
            if (fmt == 3 && bits == 32) std::memcpy(&v, &b[o], 4);
            else if (bits == 16) v = float(int16_t(u16(o))) / 32768.0f;
            else if (bits == 24) v = float(int32_t((u16(o) | uint32_t(b[o + 2]) << 16) << 8) >> 8) / 8388608.0f;
            else if (bits == 32) v = float(int32_t(u32(o))) / 2147483648.0f;
            m_inFile[i * 2 + c] = v;
        }
    printf("[AUDIO-IN] %s: %zu frames, %u ch, %u bit%s, %u Hz%s\n", path, frames, ch, bits, fmt == 3 ? " float" : "",
           rate, rate == 48000 ? "" : " - NOT 48 kHz, played as is");
}

void ms2kAudioWorkerThread();   // thread_prio_win.cpp

void DSP56362Emulator::startThread()
{
    if (m_thr.joinable() || !m_dsp) return;
    m_fsShadow = m_fsAccum;
    m_thrStop = false;
    m_thr = std::thread([this] {
        ms2kAudioWorkerThread();   // DSP-THREAD b: MMCSS Pro Audio, no power throttling
        for (;;) {
            const uint32_t d = m_qDone.load(std::memory_order_relaxed);
            if (d != m_qHead.load(std::memory_order_acquire)) {
                const DspReq r = m_q[d & (kQ - 1)];
                runForMcuCycles(r.cycles, r.hz);
                m_qDone.store(d + 1, std::memory_order_release);
                continue;
            }
            if (m_thrStop.load(std::memory_order_acquire)) break;
            // idle: spin a little (the next chunk is usually ~20 us of MCU time away), then sleep until posted
            bool got = false;
            for (int i = 0; i < 400 && !got; ++i) { _mm_pause(); got = m_qHead.load(std::memory_order_acquire) != d; }
            if (got) continue;
            std::unique_lock<std::mutex> l(m_thrMx);
            m_thrSleeping.store(true, std::memory_order_release);
            m_thrCv.wait_for(l, std::chrono::milliseconds(1), [&] { return m_qHead.load(std::memory_order_acquire) != d || m_thrStop.load(); });
            m_thrSleeping.store(false, std::memory_order_release);
        }
    });
    printf("[DSP56362] DSP-THREAD: the DSP runs on its own thread\n");
}

void DSP56362Emulator::stopThread()
{
    if (!m_thr.joinable()) return;
    drain();
    { std::lock_guard<std::mutex> l(m_thrMx); m_thrStop = true; }
    m_thrCv.notify_one();
    m_thr.join();
    m_fsAccum = m_fsShadow;   // the same value - every posted chunk has run
}

DSP56362Emulator::~DSP56362Emulator()
{
    stopThread();
    if (m_dsp) {
        printf("[DSP56362] end: %llu ESAI TX frames over %.3f s of MCU time, SHI words in %llu, RX overruns %llu\n",
               (unsigned long long)m_txFrames, double(m_mcuCyclesRun) / 10e6, (unsigned long long)m_wordsIn, (unsigned long long)m_shiOverruns);   // phi = 10 MHz
        printf("[DSP56362] non-zero TX words per line, slot0/slot1: TX0 %llu/%llu TX1 %llu/%llu TX2 %llu/%llu "
               "TX3 %llu/%llu TX4 %llu/%llu TX5 %llu/%llu\n",
               m_txNonZero[0][0], m_txNonZero[1][0], m_txNonZero[0][1], m_txNonZero[1][1],
               m_txNonZero[0][2], m_txNonZero[1][2], m_txNonZero[0][3], m_txNonZero[1][3],
               m_txNonZero[0][4], m_txNonZero[1][4], m_txNonZero[0][5], m_txNonZero[1][5]);
        // BUG106 instrument: where the program waits, and what it has enabled (MS2K_DSPDIS=1).
        if (const char* e = std::getenv("MS2K_DSPDIS"); e && *e == '1') {
            printf("[DSP56362] end state: PC=%06X SR=%06X OMR=%06X IPRC=%06X IPRP=%06X\n",
                   m_dsp->getPC().toWord(), m_dsp->getSR().toWord(), m_dsp->regs().omr.toWord(),
                   m_periphX->read(0xFFFFFF, Nop), m_periphX->read(0xFFFFFE, Nop));   // IPRC, IPRP
            Disassembler dis(m_dsp->opcodes());
            const TWord pc0 = m_dsp->getPC().toWord();
            for (TWord a = (pc0 > 0x30 ? pc0 - 0x30 : 0), n = 0; n < 40; ++n) {
                TWord opA = 0, opB = 0; m_mem->getOpcode(a, opA, opB);
                std::string s; const auto len = dis.disassemble(s, opA, opB, 0, 0, a);
                printf("[DSP-DIS] P:%06X %06X %s\n", a, opA, s.c_str());
                a += len ? len : 1;
            }
            // MS2K_DSPDISAT="hex:count,hex:count" - more ranges of P memory, disassembled.
            if (const char* r = std::getenv("MS2K_DSPDISAT"); r && *r) {
                std::string spec(r); size_t pos = 0;
                while (pos < spec.size()) {
                    size_t comma = spec.find(',', pos); if (comma == std::string::npos) comma = spec.size();
                    const std::string it = spec.substr(pos, comma - pos);
                    const size_t colon = it.find(':');
                    TWord a = TWord(std::strtoul(it.substr(0, colon).c_str(), nullptr, 16));
                    const unsigned cnt = colon == std::string::npos ? 32u : unsigned(std::strtoul(it.substr(colon + 1).c_str(), nullptr, 10));
                    for (unsigned n = 0; n < cnt; ++n) {
                        TWord opA = 0, opB = 0; m_mem->getOpcode(a, opA, opB);
                        std::string s; const auto len = dis.disassemble(s, opA, opB, 0, 0, a);
                        printf("[DSP-DIS] P:%06X %06X %s\n", a, opA, s.c_str());
                        a += len ? len : 1;
                    }
                    pos = comma + 1;
                }
            }
            printf("[DSP-Y] Y:$00-$1F:");
            for (TWord a = 0; a < 0x20; ++a) printf(" %06X", m_mem->get(MemArea_Y, a));
            printf("\n");
            for (TWord a = 0; a < 0x40; a += 2) {   // interrupt vector table
                TWord opA = 0, opB = 0; m_mem->getOpcode(a, opA, opB);
                std::string s; dis.disassemble(s, opA, opB, 0, 0, a);
                printf("[DSP-VEC] P:%06X %06X %06X %s\n", a, opA, opB, s.c_str());
            }
        }
    }
    if (m_wavPath.empty() || m_wav.empty()) return;
    // 32-bit PCM WAV, 48 kHz stated (FS2 = EXTAL/64 = 48 kHz, KOD-A30412), samples <<8.
    FILE* f = std::fopen(m_wavPath.c_str(), "wb");
    if (!f) return;
    const uint32_t n = uint32_t(m_wav.size()), bytes = n * 4, rate = 48000;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (int32_t s : m_wav) { const int32_t v = s * 256; std::fwrite(&v, 4, 1, f); }
    std::fclose(f);
    printf("[DSP56362] wrote %u frames to %s\n", n / 2, m_wavPath.c_str());
}

bool DSP56362Emulator::initialize(uint32_t clockHz, uint32_t /*sampleRate*/)
{
    if (m_dsp) return true;
    m_clockHz = clockHz ? clockHz : 3072000;
    // MS2K_DSPSPEED=<factor> (2026-09-26, R2 EXPERIMENT, default off): a faster DSP - EXTAL, and with it the
    // core clock and the ESAI slot period in cycles, times <factor>, so each sample gets that many more
    // DSP cycles. Measures how far the program is from its cycle budget; not a hardware setting.
    if (const char* e = std::getenv("MS2K_DSPSPEED"); e && *e) {
        const double k = std::atof(e);
        if (k > 0.1 && k < 10.0) { m_clockHz = uint32_t(double(m_clockHz) * k); printf("[DSP56362] MS2K_DSPSPEED=%.3f: EXTAL %u Hz (EXPERIMENT)\n", k, m_clockHz); }
    }

    const char* env = std::getenv("MS2K_DSPROM");
    const std::string path = (env && *env) ? env : "full FW/boot-362.ms2000.bin";
    std::ifstream f(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    if (rom.size() != 768) {
        printf("[DSP56362] NOT BUILT - boot ROM '%s' %s (need 768 bytes = 192 words x 4, LE). "
               "The ROM is local-only; see CLAUDE.md 'THE VINTAGE BOOT ROM'.\n",
               path.c_str(), rom.empty() ? "not found" : "has the wrong size");
        return false;
    }

    m_validator = std::make_unique<DefaultMemoryValidator>();

    // BUG119, 2026-09-25 - THE DSP'S EXTERNAL DRAM, AS THE BOARD WIRES IT.
    //
    // KOD-A30412 (service manual p.15, the JPG, zoomed): IC14 = KM416V1204CT, "16MDRAM 1Mx16",
    // on the DSP's Port A. Address: DSP A0-A9 -> DRAM A0-A9, DSP A10-A17 "NC". Data: DSP
    // D8-D23 -> DRAM D0-D15; DSP D0-D7 go to 4.7k resistor networks (RA5/RA6) and nowhere else.
    // RAS from AA0/RAS0 ("DRAS"), CAS, WE, OE from the DSP's DRAM controller.
    // Korg's program sets (measured, MS2K_DSPMEMCENSUS): AAR0 = $400222, DCR = $FFB90F.
    //   AAR0 (DSP56300FM Table 9-4, RENDERED p.172 [9-16]): BAC = $400, BNC = 2 -> the two MSBs
    //     of the address must be 01, i.e. $400000-$7FFFFF; BYEN = 1, BXEN = BPEN = 0 -> Y space
    //     only; BAT = 10 -> DRAM.
    //   DCR (Table 9-6, RENDERED p.178-179 [9-22, 9-23]): BPS = 01 -> 10-bit column; "when the
    //     row address is driven ... the 14 MSBs of the internal address are driven on A[0-13]".
    // So the DRAM sees column = address bits 0-9 and row = bits 10-19 (only A0-A9 are wired):
    // Y:$400000-$7FFFFF is ONE 1 M-word array, seen four times over (bits 20-21 ignored).
    // Korg's upload (command $70, measured) fills Y:$490000-$4BFFFF with wavedata.pcm.
    //
    // Modelled with the library's own MmuHelper (the mechanism its MemoryBuffer uses for the
    // Virus's bridged SRAM): P, X and Y keep 16 M words each, and the four 1 M-word windows of
    // Y at $400000/$500000/$600000/$700000 map the same backing pages.
    // STATED APPROXIMATION: the DRAM stores 16 bits (D8-D23); a real read returns D0-D7 as the
    // resistor networks leave them. Here the low byte is stored and read back. The wave upload
    // writes it as 00; delay lines keep 8 more bits than the part would.
    // MS2K_DSPDRAM=flat restores the old flat memory, for A/B.
    TWord* extBuf = nullptr;
    {
        const char* e = std::getenv("MS2K_DSPDRAM");
        const bool flat = e && std::strcmp(e, "flat") == 0;
        if (!flat) {
            constexpr size_t W = sizeof(TWord), AREA = 0x1000000, DRAM = 0x100000;
            m_mmu = std::make_unique<MmuHelper>();
            auto* base = reinterpret_cast<TWord*>(m_mmu->reserveAddressRange(3 * AREA * W));
            // backing: P 16M | X 16M | Y low 4M | DRAM 1M | Y high 8M
            const size_t backingWords = AREA + AREA + 0x400000 + DRAM + 0x800000;
            bool ok = base && m_mmu->createBackingStore(backingWords * W);
            size_t off = 0;
            auto map = [&](size_t backWords, size_t words, TWord* at) {
                if (ok && !m_mmu->mapRegion(backWords * W, words * W, at)) ok = false;
            };
            TWord* P = base; TWord* X = base + AREA; TWord* Y = base + 2 * AREA;
            if (ok) {
                map(off, AREA, P);                  off += AREA;
                map(off, AREA, X);                  off += AREA;
                map(off, 0x400000, Y);              off += 0x400000;
                for (int k = 0; k < 4; ++k) map(off, DRAM, Y + 0x400000 + k * DRAM);
                off += DRAM;
                map(off, 0x800000, Y + 0x800000);   off += 0x800000;
            }
            if (ok) {   // prove the aliasing before trusting it
                Y[0x490000] = 0x123400; Y[0x3FFFFF] = 0x111100; Y[0x800000] = 0x222200;
                ok = Y[0x590000] == 0x123400 && Y[0x690000] == 0x123400 && Y[0x790000] == 0x123400
                  && Y[0x3FFFFF] == 0x111100 && Y[0x800000] == 0x222200 && X[0x490000] == 0 && P[0x490000] == 0;
                Y[0x490000] = 0; Y[0x3FFFFF] = 0; Y[0x800000] = 0;
            }
            if (ok) {
                extBuf = base;
                printf("[DSP56362] DRAM: Y:$400000-$7FFFFF = one 1M-word array (KM416V1204, A0-A9), MMU-mapped x4\n");
            } else {
                m_mmu.reset();
                printf("[DSP56362] DRAM: MMU mapping FAILED - falling back to FLAT memory (Y DRAM not aliased). "
                       "This is a known deviation from KOD-A30412, not a model.\n");
            }
        } else {
            printf("[DSP56362] DRAM: MS2K_DSPDRAM=flat - flat X/Y/P, no DRAM aliasing (A/B only)\n");
        }
    }
    m_mem       = std::make_unique<Memory>(*m_validator, 0x1000000, extBuf);
    m_periphX   = std::make_unique<Peripherals56362>();
    m_periphY   = std::make_unique<PeripheralsNop>();
    m_dsp       = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());

    // ESAI <-> codec. The library's default Audio ends in BLOCKING ring buffers (audio.cpp:
    // waitNotFull / waitNotEmpty) meant for a host audio thread that drains and feeds them. We
    // have no such thread: MEASURED, the first run with a booted DSP program froze outright -
    // CPU time stopped advancing - once the program started its ESAI. So both sides are taken
    // here, in the emulation thread:
    //   TX (DSP -> AK4522 DAC): every frame is counted, and optionally written to a WAV
    //       (MS2K_DSPWAV=<file>) - TX0 slot 0/1 as left/right (BUG128b). KOD-A30412 (BUG106 reading):
    //       only SDO0 leaves the DSP (pin 4, R168 22R, net "SDO0" to the codec); SDO1-SDO3 carry
    //       nothing but 10k pull-ups. So TX0 IS the audio out. Slot 0 = left (BUG128b, read from the parts).
    //   RX (AK4522 ADC -> DSP): an empty frame, i.e. nothing plugged into the audio inputs.
    {
        auto& esai = m_periphX->getEsai();
        // AUDIO-IN (2026-09-26): RX0 = the AK4522's SDTO (KOD-A30413: SDTO -> SDI0), slot 0 = AINL =
        // Input 1, slot 1 = AINR = Input 2 (Mode 3, LRCK low = Lch; the program's RX even handler
        // stores RX0 at Y:$6A). The frame is the ADC model's output for one host frame (or silence).
        m_adc = std::make_unique<Ak4522Adc>(48000.0);
        if (const char* a = std::getenv("MS2K_AUDIOIN"); a && *a) loadAudioInWav(a);
        static const bool rxOff = [] { const char* e = std::getenv("MS2K_RXFRAME"); return e && std::strcmp(e, "empty") == 0; }();
        esai.setReadRxCallback([this](uint64_t& idx, Audio::RxFrame& f) {
            ++idx;
            if (rxOff) { f.clear(); return; }   // A/B: the pre-AUDIO-IN empty frame
            uint32_t wl = 0, wr = 0;
            readAudioIn(wl, wr);
            f.resize(2);
            f[0] = Audio::RxSlot{ wl, 0, 0, 0 };
            f[1] = Audio::RxSlot{ wr, 0, 0, 0 };
        });
        if (const char* w = std::getenv("MS2K_DSPWAV"); w && *w) m_wavPath = w;
        esai.setWriteTxCallback([this](uint64_t& idx, const Audio::TxFrame& f) {
            ++idx; ++m_txFrames;
            for (uint32_t s = 0; s < f.size() && s < 2; ++s)          // which lines carry signal
                for (uint32_t r = 0; r < 6; ++r) if (f[s][r]) ++m_txNonZero[s][r];
            // BUG128b (2026-09-26): LEFT = SLOT 0, RIGHT = SLOT 1 of TX0, now READ from the parts:
            // AK4522 DIF1=DIF0=VD (KOD-A30413) = Mode 3, I2S, LRCK low = Lch (AK4522 datasheet
            // RENDERED p.9 mode table, p.10 Figure 4); LRCK = FS3 = the same 74HC4040 Q8 net as the
            // ESAI FST (KOD-A30412); TCCR TFSP=1 = frame start on the LOW level (DSP56362UM RENDERED
            // p.156) -> slot 0 is the LRCK-low word = left. The BUG128 panpot evidence (L63 in the
            // word written by the EVEN interrupt, Y:$6C) is kept: the library now sends the words
            // written in answer to TEDE in the even slot (esai.cpp, patch 0004).
            if (m_audioRingOn && f.size() >= 2) {
                int32_t l = int32_t(f[0][0] << 8) >> 8, r = int32_t(f[1][0] << 8) >> 8;
                if (m_dac20.load(std::memory_order_relaxed)) { l = ak4522Dac20(l); r = ak4522Dac20(r); }   // AUDIO-IN round: DAC option
                pushAudio(l, r);
            }
            if (!m_wavPath.empty() && f.size() >= 2 && m_wav.size() < 2 * 48000 * 60) {
                m_wav.push_back(int32_t(f[0][0] << 8) >> 8);      // L, sign-extend 24 -> 32
                m_wav.push_back(int32_t(f[1][0] << 8) >> 8);      // R
            }
        });
    }

    // BUG106: the ESAI is a SLAVE on the MS2000 - KOD-A30412: SCKT/SCKR (pins 14/15) = 64FS1 and
    // FST/FSR (12/13) = FS1, both from the 74HC4040 dividing the 12.288 MHz (= 256fs) crystal, so
    // the frame rate is fs = 48 kHz whatever the core clock is. The library derives its ESAI slot
    // period from the core speed (EXTAL x MF / PD, from PCTL) and a sample rate, so it is given the
    // board's EXTAL, fs, and cycle counting (the default counts instructions).
    {
        auto& clk = m_periphX->getEsaiClock();
        clk.setExternalClockFrequency(m_clockHz);
        clk.setSamplerate(48000);
        clk.setClockSource(EsxiClock::ClockSource::Cycles);
    }

    // BUG125 - IRQD IS THE DSP'S OWN PB0, AND KORG'S PCM/DWGS READS ARE DMA TRIGGERED BY IT.
    // KOD-A30412 (service manual p.15, JPG): MODD/IRQD pin 134 has R114 4.7 k to GND and the same
    // net reaches H0/PB0, pin 43. Korg's per-sample code (P:$500-$512) does `bset #0,x:$FFFFC9`
    // (HDR bit 0 = PB0 high), programs DMA channels whose DCR has DRS = 00011 = "External (IRQD pin)"
    // (DSP56300FM Table 10-5, RENDERED p.199 [10-19]; DSP56362UM Table 4-19, RENDERED p.80 [4-10]),
    // then `bclr #0,x:$FFFFC9`. "All the request sources behave as edge-triggered synchronous
    // inputs" (same FM page): the falling edge on the active-low IRQD is the request. The library
    // registers those channels as request targets but nothing ever raised IRQD, so the voice DMA
    // (Y:$490000+ wave data -> Y:$100..) never ran and every PCM/DWGS oscillator read zeros.
    // Only while PB0 is an output (HDDR bit 0 = 1); as an input the pin sits at the pull-down.
    m_periphX->getHDI08().setWriteHDRCallback([this](dsp56k::TWord v) {
        const bool pb0 = (v & 1) != 0;
        const bool out = (m_periphX->getHDI08().readHDDR() & 1) != 0;
        static const bool irqdOff = [] { const char* e = std::getenv("MS2K_IRQD"); return e && std::strcmp(e, "off") == 0; }();
        if (out && m_pb0High && !pb0 && !irqdOff) {
            ++m_irqdEdges;
            // MS2K_IRQDLOG=<n>: the channel registers at the first n IRQD edges after 4 s (diagnostic)
            static const unsigned logN = [] { const char* e = std::getenv("MS2K_IRQDLOG"); return e ? unsigned(std::strtoul(e, nullptr, 10)) : 0u; }();
            static unsigned logged = 0;
            if (logged < logN && m_mcuCyclesRun > 40000000ull) { ++logged;
                printf("[IRQD] #%llu DSP PC=%06X Y:91=%06X", (unsigned long long)m_irqdEdges, m_dsp->getPC().toWord(), m_mem->get(dsp56k::MemArea_Y, 0x91));
                static const TWord base[6] = {0xFFFFEF, 0xFFFFEB, 0xFFFFE7, 0xFFFFE3, 0xFFFFDF, 0xFFFFDB};
                for (int c = 0; c < 6; ++c) printf(" | ch%d DSR=%06X DDR=%06X DCO=%06X DCR=%06X", c,
                    TWord(m_periphX->read(base[c], Nop)), TWord(m_periphX->read(base[c] - 1, Nop)),
                    TWord(m_periphX->read(base[c] - 2, Nop)), TWord(m_periphX->read(base[c] - 3, Nop)));
                printf("\n");
            }
            // PERF-DSP-1: two clock reads per edge (~130 k edges/s) only when asked for, MS2K_IRQDTIME=1
            static const bool timeIt = [] { const char* e = std::getenv("MS2K_IRQDTIME"); return e && *e == '1'; }();
            if (timeIt) {
                const auto t0 = std::chrono::steady_clock::now();
                m_periphX->getDMA().trigger(dsp56k::DmaChannel::RequestSource::ExternalIRQD);
                m_irqdNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            } else {
                m_periphX->getDMA().trigger(dsp56k::DmaChannel::RequestSource::ExternalIRQD);
            }
        }
        m_pb0High = out ? pb0 : false;
    });

    auto cfg = m_dsp->getJit().getConfig();
    cfg.dynamicFastInterrupts = true;                 // BUG104
    if (const char* e = std::getenv("MS2K_DSPNOVOLATILE"); e && *e == '1') cfg.trackVolatilePMemory = false;   // measurement
    if (const char* e = std::getenv("MS2K_DSPSTEP"); e && *e == '1') {
        cfg.maxInstructionsPerBlock = 1;              // diagnostics: one instruction per exec()
        printf("[DSP56362] MS2K_DSPSTEP=1: JIT blocks limited to one instruction (diagnostic mode)\n");
    }
    // BUG110 measurement knobs: how often the JIT hands control back to the peripherals.
    if (const char* e = std::getenv("MS2K_DSPDOITER"); e && *e) {
        cfg.maxDoIterations = uint32_t(std::strtoul(e, nullptr, 0));
        printf("[DSP56362] MS2K_DSPDOITER: maxDoIterations=%u\n", cfg.maxDoIterations);
    }
    if (const char* e = std::getenv("MS2K_DSPBLOCK"); e && *e) {
        cfg.maxInstructionsPerBlock = uint32_t(std::strtoul(e, nullptr, 0));
        printf("[DSP56362] MS2K_DSPBLOCK: maxInstructionsPerBlock=%u\n", cfg.maxInstructionsPerBlock);
    }
    m_dsp->getJit().setConfig(cfg);

    // BUG110: interrupt priority levels and nesting (FM rev.5 p.2-14, Table 5-1 p.5-14; UM rev.3
    // Table D-2 p.D-6/D-7, Table D-3 p.D-8/D-9, IPRC/IPRP equates p.B-5/B-6). The firmware runs its
    // per-sample job in the IRQB long interrupt at IPL0 (IPRC=$80082D) while the ESAI is at IPL2
    // (IPRP=$000003); on silicon the ESAI interrupts nest into the job. MS2K_DSPNEST=off = the
    // library's old model (a long interrupt is one unit, no peripheral clock until its RTI), for A/B.
    {
        const char* e = std::getenv("MS2K_DSPNEST");
        const bool nest = !(e && std::strcmp(e, "off") == 0);
        printf("[DSP56362] interrupt priorities + nesting: %s\n", nest ? "ON (IPRC/IPRP, FM 2-14)" : "OFF (MS2K_DSPNEST=off)");
        if (nest) {
            m_dsp->setInterruptPriorityFunc([this](TWord vba) {
                const auto p = interruptPriority(vba);
                DSP::InterruptPriority r; r.level = p.first; r.rank = p.second;
                return r;
            });
        }
    }

    for (TWord i = 0; i < 192; ++i)
        m_mem->set(MemArea_P, 0xFF0000 + i, rom[4*i] | (rom[4*i+1] << 8) | (rom[4*i+2] << 16));

    // RESET deasserts: MODA-MODD latched into OMR[3:0] (DSP56362/D Table 1-8), mode 5 per
    // KOD-A30412, and execution begins at the mode's reset vector $FF0000 (UM Table 4-1).
    m_dsp->regs().omr.var = (m_dsp->regs().omr.var & ~0xF) | 0x5;
    m_dsp->setPC(0xFF0000);
    m_lastPC = 0xFF0000;

    printf("[DSP56362] BUILT - dsp56kEmu d6e4514, Peripherals56362, boot ROM '%s', mode 5 (SHI SPI "
           "slave), %u Hz core clock (PLL off, EXTAL = 64FS2)\n", path.c_str(), m_clockHz);
    return true;
}

void DSP56362Emulator::runForMcuCycles(uint32_t mcuCycles, uint32_t mcuHz)
{
    if (!m_dsp || !mcuHz) return;
    m_mcuCyclesRun += mcuCycles;

    // MS2K_DSPDUMP="t:X:start:len,t:Y:start:len,..." (2026-09-25, R2 diagnostic, read-only): at MCU
    // time t, print that memory range, 8 words per line. For A/B comparisons of voice blocks.
    {
        struct D { double t; char sp; TWord a, n; bool done; };
        static std::vector<D> dumps; static bool parsed = false;
        if (!parsed) { parsed = true;
            if (const char* e = std::getenv("MS2K_DSPDUMP")) { std::string s(e); size_t pos = 0;
                while (pos < s.size()) { size_t c = s.find(',', pos); if (c == std::string::npos) c = s.size();
                    D d{}; char sp = 'X'; unsigned a = 0, n = 0; double t = 0;
                    if (std::sscanf(s.substr(pos, c - pos).c_str(), "%lf:%c:%x:%x", &t, &sp, &a, &n) == 4) { d.t = t; d.sp = sp; d.a = a; d.n = n; dumps.push_back(d); }
                    pos = c + 1; } } }
        for (auto& d : dumps) if (!d.done && double(m_mcuCyclesRun) / 10e6 >= d.t) {
            d.done = true;
            const EMemArea ar = d.sp == 'Y' ? MemArea_Y : d.sp == 'P' ? MemArea_P : MemArea_X;
            for (TWord i = 0; i < d.n; i += 8) {
                printf("[DSPDUMP] t=%.3f %c:%06X", d.t, d.sp, d.a + i);
                for (TWord k = 0; k < 8 && i + k < d.n; ++k) {
                    const TWord a = d.a + i + k;   // X:$FFFF80+ = peripheral registers, read through the bus
                    printf(" %06X", (d.sp == 'X' && a >= 0xFFFF80) ? TWord(m_periphX->read(a, Nop)) : m_mem->get(ar, a));
                }
                printf("\n");
            }
            fflush(stdout);
        }
    }

    // MS2K_DSPMEMCENSUS=<MCU s> (2026-09-25, R2 diagnostic, read-only): once, at that time, the
    // number of non-zero words per 64 K block in X, Y and P, and the Port A registers (AAR0-3,
    // DCR) as the program left them. Question: where did the 0x70 wave upload land, and in
    // which space does the program read it back.
    {
        static const double at = [] { const char* e = std::getenv("MS2K_DSPMEMCENSUS"); return e ? std::atof(e) : -1.0; }();
        static bool done = false;
        if (!done && at >= 0 && double(m_mcuCyclesRun) / 10e6 >= at) {
            done = true;
            const char* nm[3] = {"X", "Y", "P"};
            const EMemArea ar[3] = {MemArea_X, MemArea_Y, MemArea_P};
            for (int k = 0; k < 3; ++k)
                for (TWord blk = 0; blk < 0x1000000; blk += 0x10000) {
                    unsigned nz = 0; TWord first = 0, firstV = 0;
                    const TWord end = blk + 0x10000;
                    for (TWord a = blk; a < end; ++a) {
                        if (k < 2 && a >= 0xFFFF80) break;                     // peripheral space
                        const TWord v = m_mem->get(ar[k], a);
                        if (v) { if (!nz) { first = a; firstV = v; } ++nz; }
                    }
                    if (nz) printf("[DSPMEM] %s:%06X-%06X  %6u non-zero  first %s:%06X=%06X\n",
                                   nm[k], blk, end - 1, nz, nm[k], first, firstV);
                }
            printf("[DSPMEM] AAR0=%06X AAR1=%06X AAR2=%06X AAR3=%06X DCR=%06X BCR=%06X\n",
                   m_periphX->read(0xFFFFF9, Nop), m_periphX->read(0xFFFFF8, Nop), m_periphX->read(0xFFFFF7, Nop),
                   m_periphX->read(0xFFFFF6, Nop), m_periphX->read(0xFFFFFA, Nop), m_periphX->read(0xFFFFFB, Nop));
            fflush(stdout);
        }
    }

    // BUG106 - THE CORE RUNS AT THE CLOCK ITS OWN PCTL SETS, COUNTED IN CYCLES.
    // DSP56300FM §6.2.3.4.3 + Table 6-1, RENDERED p.108-112 (printed 6-6..6-10):
    //   PEN = 0: "the internal clocks are derived directly from the EXTAL signal"
    //   PEN = 1: F_CORE = F_EXTAL x MF / (PDF x DF), MF = MF[11:0]+1, PDF = PD[3:0]+1, DF = 2^DF[2:0]
    // PEN resets to PINIT, tied low by R127 (KOD-A30412), so the boot ROM runs at EXTAL = 64FS2 =
    // 3.072 MHz. Korg's stage 2 writes PCTL = $AD0165 (measured): PD=$A, PEN=1, MF=$165 ->
    // 3.072 MHz x 358 / 11 = 99.98 MHz - the part's 100 MHz rating.
    // The old pacing called exec() once per EXTAL clock, i.e. ONE INSTRUCTION per clock at 3.072
    // MHz whatever PCTL said, and JIT blocks made even that a fiction (94 M instructions in one MCU
    // second, then 3 M). Now the target is the DSP's own cycle counter.
    const uint64_t fcore = coreHz();
    m_accum += uint64_t(mcuCycles) * fcore;
    m_cycleTarget += m_accum / mcuHz;
    m_accum %= mcuHz;
    if (m_resetHeld) {
        // DSP-RESET: RESET low - the core does not run, and IRQB (FS2 AND PORT_RESET) stays low.
        m_cycleTarget = m_dsp->getCycles();
        m_fsAccum = (m_fsAccum + uint64_t(mcuCycles) * 48000u) % mcuHz;
        return;
    }

    // IRQB = HCT08(FS2, PORT_RESET) on KOD-A30412: after reset, the 48 kHz frame clock fs itself
    // (74HC4040 Q8 of the 12.288 MHz = 256fs crystal). One falling edge per fs period. Korg's
    // program takes it: IPRC = $80082D -> IBL = 01 (enabled, IPL 0), IBL2 = 1 (edge) (UM Figure 4-2,
    // RENDERED p.79), and P:$12 = jsr $3B3. An edge-triggered request is latched once until taken.
    // STATED: the phase of fs against the ESAI frame (the same fs on the board) is not aligned.
    m_fsAccum += uint64_t(mcuCycles) * 48000u;
    while (m_fsAccum >= mcuHz) {
        m_fsAccum -= mcuHz;
        const TWord iprc = m_periphX->read(0xFFFFFF, Nop);
        const bool enabled = (iprc & 0x18) != 0, edge = (iprc & 0x20) != 0;
        if (enabled && edge) {
            if (!m_dsp->hasPendingInterrupt(Vba_IRQB)) m_dsp->injectInterrupt(Vba_IRQB);
            ++m_irqbEdges;
        } else if (enabled && !m_irqbLevelTold) {
            m_irqbLevelTold = true;
            printf("[DSP56362] IRQB enabled LEVEL-triggered (IPRC=%06X) - not modelled, no IRQB delivered\n", iprc);
        }
    }

    // MS2K_DSPYWATCH=<hex Y address>: report when that Y word changes (instrument, measurement only).
    // A leading 'X' watches X memory instead ("X31").
    static const bool watchX = [] { const char* e = std::getenv("MS2K_DSPYWATCH"); return e && (*e == 'X' || *e == 'x'); }();
    static const long ywatch = [] { const char* e = std::getenv("MS2K_DSPYWATCH");
        if (!e) return -1L; if (*e == 'X' || *e == 'x') ++e; return long(std::strtoul(e, nullptr, 16)); }();
    const EMemArea watchArea = watchX ? MemArea_X : MemArea_Y;
    // MS2K_DSPLOAD="t0:t1" (BUG125 measurement, default off): between MCU times t0 and t1, per
    // 0.25 ms bucket, the share of DSP cycles spent with PC in P:0030-02FF (the host-command main
    // loop and its handlers) against everything else (interrupt handlers and what they call).
    static double ld0 = -1, ld1 = -1;
    static const bool loadOn = [] { const char* e = std::getenv("MS2K_DSPLOAD");
        return e && std::sscanf(e, "%lf:%lf", &ld0, &ld1) == 2; }();
    if (ywatch < 0 && loadOn) {
        static uint64_t mainC = 0, allC = 0, dmaC = 0, dmaN = 0; static double bucket = -1;
        static const bool flushHook = [] { SetCrashReportHook(&dumpDspRing); return true; }(); (void)flushHook;   // stdout reaches the log on a crash
        const double t = double(m_mcuCyclesRun) / double(mcuHz);
        while (m_dsp->getCycles() < m_cycleTarget) {
            const TWord pc = m_dsp->getPC().toWord();
            const uint64_t c0 = m_dsp->getCycles();
            const uint64_t i0 = m_dsp->getInstructionCounter();
            m_dsp->exec(); ++m_execCalls;
            if (t >= ld0 && t < ld1) {
                const uint64_t dc = m_dsp->getCycles() - c0;
                // VOCODER round (2026-09-26): cycles and instructions per exec() start PC, reported at ld1.
                auto& pe = m_loadProf[pc]; pe.first += dc; pe.second += m_dsp->getInstructionCounter() - i0;
                allC += dc; if (pc >= 0x30 && pc < 0x300) mainC += dc;
                if ((pc >= 0x3A8 && pc < 0x3B3) || (pc >= 0x500 && pc < 0x523) || pc == 0x22) { dmaC += dc; if (pc == 0x3A8 || pc == 0x22) ++dmaN; }
            }
        }
        if (t >= ld1 && !m_loadProf.empty() && !m_loadProfDone) {
            m_loadProfDone = true;
            std::vector<std::pair<uint64_t, TWord>> v; uint64_t tot = 0;
            for (auto& kv : m_loadProf) { v.push_back({kv.second.first, kv.first}); tot += kv.second.first; }
            std::sort(v.rbegin(), v.rend());
            printf("[DSP-CYCPROF] %.3f-%.3f s: %llu cycles by exec() start PC\n", ld0, ld1, (unsigned long long)tot);
            for (size_t i = 0; i < v.size() && i < 30; ++i) {
                const auto& pe = m_loadProf[v[i].second];
                printf("[DSP-CYCPROF] P:%06X %6.2f%% cycles %llu instr %llu (%.2f cyc/instr)\n", v[i].second, 100.0 * double(v[i].first) / double(tot),
                       (unsigned long long)pe.first, (unsigned long long)pe.second, pe.second ? double(pe.first) / double(pe.second) : 0.0);
            }
        }
        if (t >= ld0 && t < ld1) {
            if (bucket < 0) bucket = t;
            static const double bw = [] { const char* e = std::getenv("MS2K_DSPLOAD_BUCKET"); return e ? std::atof(e) : 0.00025; }();   // VOCODER-1: bucket width, s
            if (t - bucket >= bw) {
                printf("[DSP-LOAD] t=%.5f-%.5f main loop %5.1f%% of %llu cycles, DMA path %llu cycles in %llu entries, SHI words in=%llu, RX overruns=%llu, FIFO %u, HRX reads %llu\n",
                       bucket, t, allC ? 100.0 * double(mainC) / double(allC) : 0.0, (unsigned long long)allC, (unsigned long long)dmaC, (unsigned long long)dmaN,
                       (unsigned long long)m_wordsIn, (unsigned long long)m_shiOverruns, m_periphX->getSHI().rxCount(), (unsigned long long)m_periphX->getSHI().hrxReads());
                bucket = t; mainC = allC = dmaC = dmaN = 0;
            }
        }
    } else if (ywatch < 0) {
        static const bool ringOn = [this] { const char* e = std::getenv("MS2K_DSPRING"); if (!(e && *e == '1')) return false;
            SetCrashReportHook(&dumpDspRing); g_dspRingOn = true; g_ringShi = &m_periphX->getSHI(); g_ringWordsIn = reinterpret_cast<const unsigned long long*>(&m_wordsIn); g_ringOverruns = reinterpret_cast<const unsigned long long*>(&m_shiOverruns); printf("[DSP56362] MS2K_DSPRING=1: exec() ring printed on a stack overflow\n"); return true; }();
        if (ringOn) {
            while (m_dsp->getCycles() < m_cycleTarget) {
                auto& e = g_dspRing[g_dspRingI++ % kDspRingN];
                e.pc = m_dsp->getPC().toWord(); e.sr = m_dsp->getSR().toWord(); e.sp = m_dsp->regs().sp.toWord();
                e.lc = m_dsp->regs().lc.toWord(); e.mode = unsigned(m_dsp->getProcessingMode()); e.cyc = m_dsp->getCycles(); e.fifo = m_periphX->getSHI().rxCount();
                m_dsp->exec(); ++m_execCalls;
            }
        } else {
            // PERF-DSP-2 (2026-10-01): all the blocks up to the target under one trampoline entry - the same
            // interrupt/peripheral call before every block and the same blocks (see JitTrampoline::execUntilCycles).
            // m_execCalls counts these runs now, not blocks. MS2K_DSPEXECLOOP=1 = the exec() loop (A/B).
            static const bool execLoop = [] { const char* e = std::getenv("MS2K_DSPEXECLOOP"); return e && *e == '1'; }();
            if (execLoop) { while (m_dsp->getCycles() < m_cycleTarget) { m_dsp->exec(); ++m_execCalls; } }
            else if (m_dsp->getCycles() < m_cycleTarget) { m_dsp->execUntilCycles(m_cycleTarget); ++m_execCalls; }
        }
        if (!m_batchOk.load(std::memory_order_relaxed)) m_batchOk.store((m_periphX->getEsaiClock().getPCTL() & (1u << 18)) != 0, std::memory_order_relaxed);   // PERF-131: PEN
    } else {
        static TWord last = m_mem->get(watchArea, TWord(ywatch));
        static unsigned told = 0;
        constexpr unsigned RN = 400;
        static TWord ring[RN] = {}; static unsigned ri = 0;
        while (m_dsp->getCycles() < m_cycleTarget) {
            const TWord pcBefore = m_dsp->getPC().toWord();
            ring[ri++ % RN] = pcBefore;
            // MS2K_DSPPCWATCH=<hex P address>: registers each time execution starts there (needs DSPSTEP).
            static const long pcwatch = [] { const char* e = std::getenv("MS2K_DSPPCWATCH"); return e ? long(std::strtoul(e, nullptr, 16)) : -1L; }();
            // MS2K_DSPPCWATCH_T=<MCU s> (2026-09-25): start registering only from that time;
            // MS2K_DSPPCWATCH_N=<n> hits (default 24).
            static const double pcwT = [] { const char* e = std::getenv("MS2K_DSPPCWATCH_T"); return e ? std::atof(e) : 0.0; }();
            static const unsigned pcwN = [] { const char* e = std::getenv("MS2K_DSPPCWATCH_N"); return e ? unsigned(std::strtoul(e, nullptr, 10)) : 24u; }();
            if (pcwatch >= 0 && pcBefore == TWord(pcwatch) && double(m_mcuCyclesRun) / double(mcuHz) >= pcwT) {
                static unsigned pw = 0;
                if (pw < pcwN) { ++pw;
                    printf("[DSP-PCWATCH] t=%.6f P:%06lX r:", double(m_mcuCyclesRun) / double(mcuHz), pcwatch);
                    for (int k = 0; k < 8; ++k) printf(" %06X", m_dsp->regs().r[k].toWord());
                    const TWord r7 = m_dsp->regs().r[7].toWord();
                    printf("  x:(r7..r7+5):");
                    for (TWord k = 0; k < 6; ++k) printf(" %06X", m_mem->get(MemArea_X, (r7 + k) & 0xFFFFFF));
                    printf("  x0=%06X b0=%06X  y:(r7..r7+15):", m_dsp->regs().x.var & 0xFFFFFF, TWord(m_dsp->regs().b.var & 0xFFFFFF));
                    for (TWord k = 0; k < 16; ++k) printf(" %06X", m_mem->get(MemArea_Y, (r7 + k) & 0xFFFFFF));
                    printf("  y:$91=%06X\n", m_mem->get(MemArea_Y, 0x91));
                }
            }
            m_dsp->exec();
            const TWord now = m_mem->get(watchArea, TWord(ywatch));
            // MS2K_DSPYWATCH_T=<MCU s>: report only from that time on (measurement only).
            static const double ywT = [] { const char* e = std::getenv("MS2K_DSPYWATCH_T"); return e ? std::atof(e) : 0.0; }();
            if (now != last && told < 40 && double(m_mcuCyclesRun) / double(mcuHz) >= ywT) { ++told;
                printf("[DSP-YWATCH] %c:%06lX %06X -> %06X at MCU t=%.6f, exec from PC=%06X now PC=%06X, "
                       "SHI words in=%llu\n", watchX ? 'X' : 'Y', ywatch, last, now, double(m_mcuCyclesRun) / double(mcuHz),
                       pcBefore, m_dsp->getPC().toWord(), (unsigned long long)m_wordsIn);
                printf("[DSP-YWATCH] last exec() starts:");
                for (unsigned k = 0; k < RN; ++k) printf(" %X", ring[(ri + k) % RN]);
                printf("\n[DSP-YWATCH] r0..r7:");
                for (int k = 0; k < 8; ++k) printf(" %06X", m_dsp->regs().r[k].toWord());
                printf("  n4..n6: %06X %06X %06X  SR=%06X\n", m_dsp->regs().n[4].toWord(),
                       m_dsp->regs().n[5].toWord(), m_dsp->regs().n[6].toWord(), m_dsp->getSR().toWord());
                printf("[DSP-YWATCH] n0..n7:");
                for (int k = 0; k < 8; ++k) printf(" %06X", m_dsp->regs().n[k].toWord());
                printf("  m0..m7:");
                for (int k = 0; k < 8; ++k) printf(" %06X", m_dsp->regs().m[k].toWord());
                printf("\n[DSP-YWATCH] Y:0000..005F:");
                for (TWord k = 0; k < 0x60; ++k) printf("%s%06X", (k & 15) ? " " : "\n   ", m_mem->get(MemArea_Y, k));
                printf("\n");
            }
            last = now;
        }
    }
    // MS2K_DSPPCHIST="t0:t1" (2026-09-25, R2 diagnostic, read-only): between MCU times t0 and t1, a
    // histogram of the DSP PC as seen after each MCU step (block granularity), printed at t1.
    {
        static double h0 = -1, h1 = -1; static bool parsed = false, done = false;
        static std::map<TWord, uint64_t> hist;
        if (!parsed) { parsed = true; if (const char* e = std::getenv("MS2K_DSPPCHIST")) std::sscanf(e, "%lf:%lf", &h0, &h1); }
        if (h0 >= 0 && !done) {
            const double t = double(m_mcuCyclesRun) / double(mcuHz);
            if (t >= h0 && t < h1) hist[m_dsp->getPC().toWord()]++;
            else if (t >= h1) { done = true;
                std::vector<std::pair<uint64_t, TWord>> v; uint64_t tot = 0;
                for (auto& kv : hist) { v.push_back({kv.second, kv.first}); tot += kv.second; }
                std::sort(v.rbegin(), v.rend());
                printf("[DSP-PCHIST] %.2f-%.2f s, %llu samples:\n", h0, h1, (unsigned long long)tot);
                for (size_t i = 0; i < v.size() && i < 40; ++i) printf("[DSP-PCHIST] P:%06X %6.2f%%\n", v[i].second, 100.0 * double(v[i].first) / double(tot));
            }
        }
    }
    // PERF-124: the two instruments below ran on every call (= every MCU instruction), five
    // peripheral register reads each time; every 256th call (~25 us of MCU time) is enough for what
    // they report (boot-ROM entries, PCTL/ESAI register changes, the once-a-second summary).
    if ((++m_instrumentTick & 0xFFu) != 0) return;
    traceEvents();
    // BUG106 instrument: the DSP program's own clock and ESAI setup, as it writes them.
    {
        auto& esai = m_periphX->getEsai();
        auto& clk  = m_periphX->getEsaiClock();
        const TWord now[5] = { clk.getPCTL(), esai.readTransmitControlRegister(),
                               esai.readTransmitClockControlRegister(), esai.readReceiveControlRegister(),
                               esai.readReceiveClockControlRegister() };
        static const char* nm[5] = { "PCTL", "TCR", "TCCR", "RCR", "RCCR" };
        for (int i = 0; i < 5; ++i) {
            if (now[i] != m_regSeen[i]) {
                static unsigned told = 0;
                if (told < 40) { ++told;
                    printf("[DSP56362] %s %06X -> %06X at MCU t=%.4f s, DSP PC=%06X, instr=%llu cycles=%llu, "
                           "ESAI clock: %u cycles/sample, speed %llu Hz\n", nm[i], m_regSeen[i], now[i],
                           double(m_mcuCyclesRun) / double(mcuHz), m_dsp->getPC().toWord(),
                           (unsigned long long)m_dsp->getInstructionCounter(),
                           (unsigned long long)m_dsp->getCycles(), clk.getCyclesPerSample(),
                           (unsigned long long)clk.getSpeedInHz()); }
                m_regSeen[i] = now[i];
            }
        }
        // once per MCU second: how far the DSP got
        if (m_mcuCyclesRun >= m_nextSummary) {
            m_nextSummary = m_mcuCyclesRun + mcuHz;
            static unsigned told = 0;
            static const auto wall0 = std::chrono::steady_clock::now();   // first summary = wall 0
            // first minute every second; later every 30 s, and in any second that lost SHI words (VOCODER-1 follow-up)
            static unsigned secs = 0; static uint64_t ovrSeen = 0; ++secs;
            const bool ovrNew = m_shiOverruns != ovrSeen; ovrSeen = m_shiOverruns;
            // JIT-SM detector (2026-10-01): the library's JIT does not model arithmetic saturation mode (SR SM, bit 20;
            // its interpreter does). This firmware never sets it (measured: RESO-ART 1). Should it ever, say so once.
            { static bool smTold = false;
              if (!smTold && (m_dsp->getSR().toWord() & 0x100000)) { smTold = true;
                  printf("[DSP56362] WARNING: SR.SM (arithmetic saturation) is set at PC=%06X - the JIT does not model it\n", m_dsp->getPC().toWord()); } }
            if (told < 60 || ovrNew || secs % 30 == 0) { ++told;
                printf("[DSP56362] t=%.2f s: PC=%06X SR=%06X core=%llu Hz instr=%llu cycles=%llu TX frames=%llu "
                       "IRQB edges=%llu IRQD(PB0) edges=%llu (%.3f s in DMA) exec()=%llu SHI words in=%llu (RX overruns %llu) wall=%.2f s sync calls %llu runs %llu fs runs %llu\n",
                       double(m_mcuCyclesRun) / double(mcuHz), m_dsp->getPC().toWord(), m_dsp->getSR().toWord(),
                       (unsigned long long)coreHz(),
                       (unsigned long long)m_dsp->getInstructionCounter(),
                       (unsigned long long)m_dsp->getCycles(), (unsigned long long)m_txFrames,
                       (unsigned long long)m_irqbEdges, (unsigned long long)m_irqdEdges, double(m_irqdNs) * 1e-9, (unsigned long long)m_execCalls, (unsigned long long)m_wordsIn, (unsigned long long)m_shiOverruns,
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count(),
                       (unsigned long long)m_syncCalls, (unsigned long long)m_syncRuns, (unsigned long long)m_fsRuns); }
        }
    }
    // ESAI output rate, measured against MCU time: the first frame, then every 48000.
    if (m_txFrames && (m_txReported == 0 || m_txFrames >= m_txReported + 48000)) {
        static unsigned told = 0;
        if (told < 12) { ++told;
            printf("[DSP56362] ESAI TX frame #%llu at MCU t=%.4f s\n",
                   (unsigned long long)m_txFrames, double(m_mcuCyclesRun) / double(mcuHz)); }
        m_txReported = m_txFrames;
    }
}

// BUG110. Level of each source from IPRC ($FFFFFF) / IPRP ($FFFFFE), field 00 = disabled, 01/10/11 =
// IPL 0/1/2 (UM rev.3 equates p.B-5/B-6, Fig. 4-2); vectors from Table D-2 (p.D-6/D-7); rank = the
// order of Table D-3 (p.D-8/D-9), lower is served first within an IPL.
// PERF-DSP-1 (2026-10-01): the library asks this for every pending request on every exec() while one is
// pending (masked requests stay pending through the whole IRQB job) - two peripheral-bus reads and a switch
// each time. The answer depends only on (vba, IPRC, IPRP): kept as a 256-entry table, rebuilt when either
// register differs from the values it was built from. Same results, same one-shot report for reserved vectors.
std::pair<int, int> DSP56362Emulator::interruptPriority(uint32_t vba)
{
    if (vba >= 0x100) return interruptPriorityCalc(vba, 0, 0);
    if (vba < 0x10) return { 3, int(vba >> 1) };
    const TWord iprc = m_periphX->read(0xFFFFFF, Nop);
    const TWord iprp = m_periphX->read(0xFFFFFE, Nop);
    if (!m_prioValid || iprc != m_prioIprc || iprp != m_prioIprp) {
        for (uint32_t v = 0; v < 0x100; ++v) {
            const auto p = interruptPriorityCalc(v, iprc, iprp);
            m_prioLevel[v] = int8_t(p.first); m_prioRank[v] = uint8_t(p.second);
        }
        m_prioIprc = iprc; m_prioIprp = iprp; m_prioValid = true;
    }
    const int lvl = m_prioLevel[vba];
    if (lvl < 0 && m_prioRank[vba] == 0xFF && !(m_unknownVectorTold[vba >> 6] & (1ull << (vba & 63)))) {
        m_unknownVectorTold[vba >> 6] |= 1ull << (vba & 63);
        printf("[DSP56362] BUG110: request on reserved vector $%02X - no source in Table D-2, dropped\n", vba);
    }
    return { lvl, lvl < 0 && m_prioRank[vba] == 0xFF ? 0 : int(m_prioRank[vba]) };
}

std::pair<int, int> DSP56362Emulator::interruptPriorityCalc(uint32_t vba, TWord iprc, TWord iprp)
{
    if (vba < 0x10) {
        // level 3: RESET, stack error, illegal, debug, trap, NMI, 2 reserved - Table D-3 order = vector order
        return { 3, int(vba >> 1) };
    }
    auto lvl = [](TWord reg, int bit) { const int f = int((reg >> bit) & 3); return f - 1; };

    switch (vba) {
    case 0x10: return { lvl(iprc, 0), 0 };                 // IRQA  IAL1:0
    case 0x12: return { lvl(iprc, 3), 1 };                 // IRQB  IBL1:0
    case 0x14: return { lvl(iprc, 6), 2 };                 // IRQC
    case 0x16: return { lvl(iprc, 9), 3 };                 // IRQD
    case 0x18: case 0x1A: case 0x1C: case 0x1E: case 0x20: case 0x22: {
        const int ch = int((vba - 0x18) >> 1);             // DMA0..5  D0L..D5L at bits 12,14,..,22
        return { lvl(iprc, 12 + 2 * ch), 4 + ch };
    }
    case 0x34: return { lvl(iprp, 0), 10 };                // ESAI RX data with exception
    case 0x32: return { lvl(iprp, 0), 11 };                // ESAI RX even data
    case 0x30: return { lvl(iprp, 0), 12 };                // ESAI RX data
    case 0x36: return { lvl(iprp, 0), 13 };                // ESAI RX last slot
    case 0x3C: return { lvl(iprp, 0), 14 };                // ESAI TX data with exception
    case 0x3E: return { lvl(iprp, 0), 15 };                // ESAI TX last slot
    case 0x3A: return { lvl(iprp, 0), 16 };                // ESAI TX even data
    case 0x38: return { lvl(iprp, 0), 17 };                // ESAI TX data
    case 0x4C: return { lvl(iprp, 2), 18 };                // SHI bus error
    case 0x4A: return { lvl(iprp, 2), 19 };                // SHI RX overrun
    case 0x42: return { lvl(iprp, 2), 20 };                // SHI TX underrun
    case 0x48: return { lvl(iprp, 2), 21 };                // SHI RX FIFO full
    case 0x40: return { lvl(iprp, 2), 22 };                // SHI TX data
    case 0x44: return { lvl(iprp, 2), 23 };                // SHI RX FIFO not empty
    case 0x28: return { lvl(iprp, 6), 27 };                // DAX TX underrun
    case 0x2A: return { lvl(iprp, 6), 28 };                // DAX block transferred
    case 0x2E: return { lvl(iprp, 6), 29 };                // DAX TX register empty
    case 0x56: return { lvl(iprp, 8), 30 };                // TIMER0 overflow
    case 0x54: return { lvl(iprp, 8), 31 };                // TIMER0 compare
    case 0x5A: return { lvl(iprp, 8), 32 };                // TIMER1 overflow
    case 0x58: return { lvl(iprp, 8), 33 };                // TIMER1 compare
    case 0x5E: return { lvl(iprp, 8), 34 };                // TIMER2 overflow
    case 0x5C: return { lvl(iprp, 8), 35 };                // TIMER2 compare
    case 0x60: return { lvl(iprp, 4), 25 };                // HDI08 receive data full
    case 0x62: return { lvl(iprp, 4), 26 };                // HDI08 transmit data empty
    default: break;
    }
    if (vba >= 0x64 && vba < 0x100)
        return { lvl(iprp, 4), 24 };                       // HDI08 host command (HCVR sets the vector)

    // Reserved in Table D-2. Nothing on this board should request it - interruptPriority() says so once, loudly.
    return { -1, 0xFF };   // rank 0xFF marks "reserved" for the caller; reported as rank 0 as before
}

void DSP56362Emulator::setPortReset(bool high)
{
    if (!m_dsp || high == !m_resetHeld) return;
    syncMcu();                                   // PERF-131: the DSP runs up to this MCU instruction first
    static unsigned told = 0;
    if (!high) {
        m_resetHeld = true;
        if (told < 16) { ++told; printf("[DSP56362] RESET asserted (PORT_RESET low) at MCU t=%.6f, PC=%06X\n",
                                        double(m_mcuCyclesRun) / 10e6, m_dsp->getPC().toWord()); }
        return;
    }
    // Rising edge. DSP56300FM 2.3.1 / resetHW(): registers to their reset values; UM Table 4-1 +
    // DSP56362/D Table 1-8: MODA-D latched into OMR[3:0] - mode 5 on this board (MODB = FS2 AND PORT_RESET
    // is 0 while RESET is low); PCTL resets with PEN = PINIT = 0 (R127), so the core runs at EXTAL until
    // the program sets the PLL again. STATED: timers are not reset (not used by this firmware); the
    // reset is taken at an MCU instruction boundary.
    m_resetHeld = false;
    ++m_resetReleases;
    m_dsp->resetPin();
    m_periphX->getDMA().reset();
    m_periphX->getEsaiClock().setPCTL(0);
    m_dsp->regs().omr.var = (m_dsp->regs().omr.var & ~0xF) | 0x5;
    m_dsp->setPC(0xFF0000);
    m_cycleTarget = m_dsp->getCycles();
    m_accum = 0;
    m_batchOk = false;
    m_pb0High = false;
    m_byteIdx = 0; m_inWord = 0; m_frameBytes = 0; m_frameWordsN = 0;
    if (told < 16) { ++told; printf("[DSP56362] RESET released (PORT_RESET high) at MCU t=%.6f: mode 5, PC=$FF0000 (#%llu)\n",
                                    double(m_mcuCyclesRun) / 10e6, (unsigned long long)m_resetReleases); }
}

uint64_t DSP56362Emulator::coreHz() const
{
    const TWord pctl = m_periphX->getEsaiClock().getPCTL();
    // PERF-124: called once per MCU instruction; the 64-bit divide below only when PCTL changes.
    static thread_local TWord lastPctl = 0xFFFFFFFFu; static thread_local uint32_t lastClock = 0; static thread_local uint64_t lastHz = 0;
    if (pctl == lastPctl && m_clockHz == lastClock) return lastHz;
    lastPctl = pctl; lastClock = m_clockHz;
    if (!(pctl & (1u << 18))) return lastHz = m_clockHz;          // PEN = 0: EXTAL
    const uint64_t pdf = ((pctl >> 20) & 15u) + 1u;
    const uint64_t df  = 1ull << ((pctl >> 12) & 7u);
    const uint64_t mf  = (pctl & 0xFFFu) + 1u;
    return lastHz = uint64_t(m_clockHz) * mf / (pdf * df);
}

void DSP56362Emulator::traceEvents()
{
    // Report entries into the boot ROM (the reset and every "jump back to the bootloader").
    const uint32_t pc = m_dsp->getPC().toWord();
    const bool inRom = pc >= 0xFF0000 && pc <= 0xFF00BF;
    const bool wasInRom = m_lastPC >= 0xFF0000 && m_lastPC <= 0xFF00BF;
    if (inRom && !wasInRom && m_romEntries < 16) {
        ++m_romEntries;
        printf("[DSP56362] entered the boot ROM (#%u) from PC=%06X, OMR=%06X, words received so far %llu\n",
               m_romEntries, m_lastPC, m_dsp->regs().omr.toWord(), (unsigned long long)m_wordsIn);
    }
    if (!inRom && wasInRom) {
        static unsigned told = 0;
        if (told < 16) { ++told;
            printf("[DSP56362] left the boot ROM for PC=%06X (OMR=%06X) after %llu words\n",
                   pc, m_dsp->regs().omr.toWord(), (unsigned long long)m_wordsIn); }
    }
    m_lastPC = pc;
}

// SS edge (PF1, active low; the firmware's BCLR/BSET #1,@PFDR at 0x010C6A / 0x010C2C).
// Our byte counter restarts on every edge. STATED APPROXIMATION: what the SHI does with a
// partial word when SS rises is not yet checked against the UM's SPI slave timing section.
void DSP56362Emulator::setSS(bool asserted)
{
    if (asserted == m_ssWas) return;
    if (!asserted && m_dsp) {
        static unsigned told = 0;
        if (told < 32) { ++told;
            printf("[DSP56362] SS released: %llu bytes in this frame, %llu words received in total, "
                   "HCSR=%06X PC=%06X\n", (unsigned long long)m_frameBytes, (unsigned long long)m_wordsIn,
                   m_periphX->getSHI().read(SHI::HCSR), m_dsp->getPC().toWord()); }
        // MS2K_SHITRACE=<MCU seconds>: every frame from then on - its words both ways (first 6).
        static const double from = [] { const char* e = std::getenv("MS2K_SHITRACE"); return e ? std::atof(e) : -1.0; }();
        static unsigned traced = 0;
        // MS2K_SHITRACE_N=<count> (default 400); MS2K_SHITRACE_QUIET=1 leaves out the one-word
        // frames of the D0 status queries and their FFFFFF read-backs, so a command stream shows.
        static const unsigned cap = [] { const char* e = std::getenv("MS2K_SHITRACE_N"); return e ? unsigned(std::strtoul(e, nullptr, 0)) : 400u; }();
        static const bool quiet = [] { const char* e = std::getenv("MS2K_SHITRACE_QUIET"); return e && *e == '1'; }();
        const bool noise = quiet && m_frameWordsN == 1 &&
                           (m_frameIn[0] == 0xFFFFFFu || (m_frameIn[0] >> 16) == 0xD0u);
        if (from >= 0 && double(m_mcuCyclesRun) / 10e6 >= from && traced < cap && !noise) {
            ++traced;
            printf("[SHI-FRAME] t=%.4f DSP PC=%06X %llu B  MOSI:", double(m_mcuCyclesRun) / 10e6,
                   m_dsp->getPC().toWord(), (unsigned long long)m_frameBytes);
            for (uint32_t i = 0; i < m_frameWordsN; ++i) printf(" %06X", m_frameIn[i]);
            printf("  MISO:");
            for (uint32_t i = 0; i < m_frameWordsN; ++i)     // '!' = stale: HTX not refilled (underrun)
                printf(" %06X%s", m_frameOut[i] & 0xFFFFFF, (m_frameOut[i] & 0x80000000u) ? "!" : "");
            printf("\n");
        }
    }
    m_frameWordsN = 0;
    m_byteIdx = 0; m_inWord = 0; m_frameBytes = 0;
    m_ssWas = asserted;
}

uint8_t DSP56362Emulator::spiByte(uint8_t mosi, bool ssAsserted)
{
    if (!m_dsp) return 0xFF;
    auto& shi = m_periphX->getSHI();

    if (ssAsserted != m_ssWas) setSS(ssAsserted);
    if (!ssAsserted) return 0xFF;                // MISO tri-stated, R249 pull-up

    const TWord hcsr = shi.read(SHI::HCSR);
    if (!(hcsr & 1) || (hcsr & 2)) {             // HEN = 0, or HI2C = 1: SPI clocks are ignored
        static bool told = false;
        if (!told) { told = true;
            printf("[DSP56362] SPI byte 0x%02X while HCSR=%06X (HEN=%u HI2C=%u) - not an SPI slave, "
                   "the byte is not taken\n", mosi, hcsr, hcsr & 1, (hcsr >> 1) & 1); }
        return 0xFF;
    }
    // HM[1:0] (UM RENDERED p.129, Table 7-4): 00 = 8, 01 = 16, 10 = 24 bit, 11 reserved.
    const uint32_t hm = (hcsr >> 2) & 3;
    const uint32_t bytesPerWord = hm == 0 ? 1 : hm == 1 ? 2 : 3;

    ++m_frameBytes;
    if (m_byteIdx == 0) { m_outWord = shi.peekTx(); m_outFresh = shi.hasTxData(); }   // what the shift register will send
    const uint32_t shift = 8 * (bytesPerWord - 1 - m_byteIdx);
    const uint8_t  miso  = uint8_t(m_outWord >> shift);
    m_inWord = (m_inWord << 8) | mosi;
    if (++m_byteIdx == bytesPerWord) {
        // BUG125 measurement: a word the SHI discards (RX FIFO full -> HROE, UM SHI chapter)
        // is counted, the first few logged with the DSP PC and the MCU time.
        const TWord rxBefore = shi.read(SHI::HCSR);
        shi.exchange(m_inWord & ((1u << (8 * bytesPerWord)) - 1u));
        if ((rxBefore >> 19) & 1) {                   // HRFF was set: this word was discarded
            ++m_shiOverruns;
            if (m_shiOverruns == 1 && g_dspRingOn) dumpDspRing();   // VOCODER-1: what the DSP ran before the first loss
            if (m_shiOverruns <= 8)
                printf("[DSP56362] SHI RX overrun #%llu: word %06X discarded at MCU t=%.6f, DSP PC=%06X, HCSR=%06X\n",
                       (unsigned long long)m_shiOverruns, m_inWord, double(m_mcuCyclesRun) / 10e6,
                       m_dsp->getPC().toWord(), rxBefore);
        }
        if (m_frameWordsN < 64) { m_frameIn[m_frameWordsN] = m_inWord;
                                 m_frameOut[m_frameWordsN] = m_outWord | (m_outFresh ? 0 : 0x80000000u); ++m_frameWordsN; }
        ++m_wordsIn;
        m_byteIdx = 0; m_inWord = 0;
    }
    return miso;
}

bool DSP56362Emulator::hreqAsserted() const
{
    if (!m_dsp) return false;
    const TWord hcsr = m_periphX->getSHI().read(SHI::HCSR);
    const uint32_t hrqe = (hcsr >> 7) & 3;
    if (!(hcsr & 1) || hrqe == 0) return false;      // HREQ tri-stated
    if (hrqe == 1) return ((hcsr >> 19) & 1) == 0;   // receive: asserted while the FIFO is not full
    return false;                                    // HRQE 10/11: not modelled yet (not used by the ROM)
}

} // namespace MS2000
