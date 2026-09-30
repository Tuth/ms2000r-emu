// AUDIO-IN round (2026-09-26): the AK4522 ADC model against the datasheet numbers it was built from
// (docs/AK4522 (1).PDF RENDERED p.5, p.6, p.10) and the KOD-A30413 input-stage gains.
#include "core/ak4522.h"
#include <cmath>
#include <cstdio>
using namespace MS2000;

static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { ++fails; std::printf("[AK4522-FAIL] %s\n", what); } else std::printf("[AK4522-OK]   %s\n", what); }

// steady-state gain of the HPF at f, 48 kHz, input amplitude a volts
static double hpfGainDb(double f, double fsAdj = 48000.0)
{
    Ak4522Adc adc(fsAdj);
    const double a = 0.5;                       // volts: a third of full scale
    double peak = 0;
    const int n = int(fsAdj * 12.0);            // 12 s: > 10 time constants at 1 Hz
    for (int i = 0; i < n; ++i) {
        uint32_t wl, wr; bool cl, cr;
        adc.process(a * std::sin(2 * 3.14159265358979 * f * i / fsAdj), 0.0, wl, wr, cl, cr);
        if (i > n - int(fsAdj / f) * 2) { const double v = double(int32_t(wl << 8) >> 8) / 8388608.0 * 1.5; if (std::fabs(v) > peak) peak = std::fabs(v); }
    }
    return 20 * std::log10(peak / a);
}

int main()
{
    // p.6 at fs = 44.1 kHz: -3 dB 0.9 Hz, -0.5 dB 2.7 Hz, -0.1 dB 6.0 Hz (first order: within 7 %)
    const double g09 = hpfGainDb(0.9, 44100), g27 = hpfGainDb(2.7, 44100), g60 = hpfGainDb(6.0, 44100), g1k = hpfGainDb(1000, 44100);
    std::printf("[AK4522] HPF @44.1k: 0.9 Hz %.2f dB, 2.7 Hz %.2f dB, 6.0 Hz %.2f dB, 1 kHz %.3f dB\n", g09, g27, g60, g1k);
    check(std::fabs(g09 + 3.0) < 0.3, "HPF -3 dB at 0.9 Hz (p.6)");
    check(std::fabs(g27 + 0.5) < 0.1, "HPF -0.5 dB at 2.7 Hz (p.6)");
    check(std::fabs(g60 + 0.1) < 0.03, "HPF -0.1 dB at 6.0 Hz (p.6)");
    check(std::fabs(g1k) < 0.01, "flat at 1 kHz");
    // 20 bits, MSB-aligned in the 24-bit slot (p.5, p.10); group delay 29 frames (29.3/fs, p.6)
    {
        Ak4522Adc adc; int firstNonZero = -1; bool lowNibbleZero = true;
        for (int i = 0; i < 64; ++i) {
            uint32_t wl, wr; bool cl, cr;
            adc.process(i >= 0 ? 0.3 : 0.0, 0.0, wl, wr, cl, cr);
            if (wl & 0xF) lowNibbleZero = false;
            if (wl && firstNonZero < 0) firstNonZero = i;
        }
        check(lowNibbleZero, "20-bit words: low 4 bits of the slot are zero");
        std::printf("[AK4522] first output after %d frames\n", firstNonZero);
        check(firstNonZero == 29, "group delay 29 frames");
    }
    // full scale 1.5 V peak per pin (p.5 note 7): 1.6 V clips, 1.4 V does not
    {
        Ak4522Adc adc; uint32_t wl, wr; bool cl = false, cr = false, any = false;
        adc.process(1.4, 1.6, wl, wr, cl, cr); any = cl;
        check(!any && cr, "clip flag at the 1.5 V full scale");
    }
    // KOD-A30413 gains; SW1 MIC/LINE = 29.7 dB vs the legend's -40/-10 dBu
    check(std::fabs(Ms2kInputStage::kGainIn1 - 3.136) < 0.001, "Input 1 gain 1 + 47k/22k");
    check(std::fabs(20 * std::log10(Ms2kInputStage::kGainIn2Mic / Ms2kInputStage::kGainIn2Line) - 29.7) < 0.1, "SW1 MIC/LINE 29.7 dB");
    check(ms2kAudioTaper(0.5) == 0.1 && ms2kAudioTaper(1.0) == 1.0 && ms2kAudioTaper(0.0) == 0.0, "10 kA taper: 10 % at mid-travel");
    check(ak4522Dac20(0x12345F) == 0x123450 && ak4522Dac20(-1) == -16, "DAC takes the 20 MSBs");
    std::printf(fails ? "[AK4522] %d FAIL\n" : "[AK4522] all passed\n", fails);
    return fails ? 1 : 0;
}
