#pragma once
// ===========================================================================
// AK4522 CODEC + THE MS2000 AUDIO INPUT STAGE (2026-09-26, AUDIO-IN round).
//
// Everything below is read, not assumed. Sources:
//   AKM AK4522 datasheet M0020-E-01 (docs/AK4522 (1).PDF), RENDERED pages:
//     p.5  ADC and DAC resolution 20 bits. ADC input 3.0 Vpp typ (AIN = 0.6 x VREFH); note 7:
//          "Full scale input for each AIN+/- pin is 1.5Vpp in differential mode".
//     p.6  ADC digital HPF: -3 dB 0.9 Hz, -0.5 dB 2.7 Hz, -0.1 dB 6.0 Hz at fs = 44.1 kHz;
//          ADC group delay 29.3/fs; DAC group delay 14.7/fs.
//     p.10 Mode 3 timing: 20-bit words, MSB first (bits 19..0), the rest of the slot "Don't Care";
//          "The cut-off frequency of the HPF is 0.9Hz at fs=44.1kHz and also scales with sampling rate".
//     p.11 Table 3: DEM1 = 0, DEM0 = 1 -> de-emphasis OFF.
//   KORG KOD-A30413 "Analog Input / Output Schematic" (service manual p.16, JPG):
//     DIF1 = DIF0 = DEM0 = VD, DEM1 = GND -> Mode 3 I2S, de-emphasis off.
//     SMUTE: only R145 (to GND) and R151 (to CODEC_MUTE), both "NU" - nothing drives it.
//     Input 1 (Line), PH4: IC8-A -47k/47k -> VR30 (10 kA, panel KLM-2181) -> IC8-B 1 + 47k/22k
//          -> IC12-B -22k/22k -> AINL+ (AINL- to the bias).                     net +3.136 x VR30
//     Input 2 (Line/Mic), PH3: IC2-A -1 -> VR31 (10 kA) -> IC2-B 1 + Rf/(R35 || R36 = 11k),
//          SW1: LINE Rf = R14 22k (x3.0), MIC Rf = R13 1M (x91.9) -> IC12-A -1 -> AINR+.
//          Check: 91.9 / 3.0 = 29.7 dB; the panel legend says MIC -40 dBu / LINE -10 dBu = 30 dB.
//
// The ADC HPF is modelled first order: -3 dB at fc, and then -0.5 dB falls at 2.86 fc and -0.1 dB
// at 6.6 fc - the datasheet's 2.7 and 6.0 Hz for fc = 0.9 Hz, within 7 %.
//
// STATED, not read: (1) the ADC's decimation filter is not modelled - the host signal already comes
// band-limited at 48 kHz; (2) the group delay is taken as 29 samples (29.3/fs); (3) the 10 kA pots
// follow the two-segment "10 % at mid-travel" audio-taper law, not a measured curve; (4) the host's
// digital full scale is mapped to the jack voltage that just reaches ADC full scale on Input 1 with
// VR30 fully up (1.5 V / 3.136 = 0.478 V peak) - a PC has no absolute voltage to give us; (5) which
// net reaches AINL and which AINR was followed by eye on the JPG: Input 1 -> R100 -> IC12-B -> AINL.
// ===========================================================================
#include <cmath>
#include <cstdint>

namespace MS2000 {

// VR30 / VR31, 10 kA: two-segment audio taper, 10 % of the voltage at mid-travel. STATED (3).
inline double ms2kAudioTaper(double pos)
{
    if (pos <= 0.0) return 0.0;
    if (pos >= 1.0) return 1.0;
    return pos < 0.5 ? pos * 0.2 : 0.1 + (pos - 0.5) * 1.8;
}

struct Ms2kInputStage {
    static constexpr double kGainIn1     = 1.0 + 47.0 / 22.0;          // IC8-B
    static constexpr double kGainIn2Line = 1.0 + 22.0 / 11.0;          // IC2-B, SW1 LINE
    static constexpr double kGainIn2Mic  = 1.0 + 1000.0 / 11.0;        // IC2-B, SW1 MIC
    static constexpr double kAdcFullScaleV  = 1.5;                     // peak, per AIN+ pin (p.5 note 7)
    static constexpr double kHostFullScaleV = kAdcFullScaleV / kGainIn1; // STATED (4)
};

class Ak4522Adc {
public:
    explicit Ak4522Adc(double fs = 48000.0) { setRate(fs); }
    void setRate(double fs)
    {
        const double fc = 0.9 * fs / 44100.0;                          // p.10: scales with fs
        m_a = std::exp(-2.0 * 3.14159265358979323846 * fc / fs);
    }
    // One frame. l/r = volts at AINL+/AINR+ against VCOM. Returns the 24-bit slot words the ESAI
    // receives (20 significant bits, MSB-aligned), and whether the modulator was driven past full scale.
    void process(double l, double r, uint32_t& wl, uint32_t& wr, bool& clipL, bool& clipR)
    {
        wl = channel(0, l, clipL);
        wr = channel(1, r, clipR);
    }
private:
    static constexpr unsigned kDelay = 29;                             // 29.3/fs, STATED (2)
    uint32_t channel(int c, double v, bool& clip)
    {
        double x = v / Ms2kInputStage::kAdcFullScaleV;                 // full scale = +-1
        clip = x > 1.0 || x < -1.0;
        if (x > 1.0) x = 1.0; else if (x < -1.0) x = -1.0;
        const double y = m_a * (m_y[c] + x - m_x[c]);                  // first-order HPF
        m_x[c] = x; m_y[c] = y;
        double q = std::round(y * 524288.0);                           // 20 bits
        if (q > 524287.0) q = 524287.0; else if (q < -524288.0) q = -524288.0;
        const uint32_t word = (uint32_t(int32_t(q)) << 4) & 0xFFFFFFu;
        const uint32_t out = m_line[c][m_pos[c]];
        m_line[c][m_pos[c]] = word;
        m_pos[c] = (m_pos[c] + 1) % kDelay;
        return out;
    }
    double   m_a = 0.0;
    double   m_x[2] = {}, m_y[2] = {};
    uint32_t m_line[2][kDelay] = {};
    unsigned m_pos[2] = {};
};

// DAC side: Mode 3 takes bits 19..0 of the slot and ignores the rest (p.10), so the 24-bit word the
// DSP sends is heard as its 20 most significant bits. De-emphasis is off (DEM1/DEM0 = 0/1) and
// SMUTE is not driven on this board, so nothing else stands between the word and the analog filter.
inline int32_t ak4522Dac20(int32_t s24) { return s24 & ~int32_t(0xF); }

// ===========================================================================
// DAC-1 (2026-10-08): THE AK4522 DAC AND THE MS2000 LINE OUTPUT STAGE.
//   AK4522 datasheet M0020-E-01, RENDERED p.6 "DAC Digital Filter" (fs = 44.1 kHz, DEM0=1/DEM1=0):
//     passband 0..20.0 kHz at -0.06 dB, -6.0 dB at 22.05 kHz; ripple +-0.06 dB; stopband 24.1 kHz,
//     43 dB; group delay 14.7/fs. Note 10: the frequencies scale with fs -> 0.4535 fs and fs/2.
//   p.11 Table 3: DEM1/DEM0 = 0/1 = de-emphasis OFF; soft mute is SMUTE-driven, not driven here (above).
//   KORG KOD-A30413 (service manual p.16), line output, read on the JPG:
//     AOUTL/R -> C126/C118 10uF -> Master VR (RK0971221Z05, MVR PCB KLM-2183) -> C134/C117 10uF
//     -> IC19 + input, R158/R135 22k to the bias (B) -> IC19-A/B x2 (R157/R159, R134/R129 47k/47k,
//     22pF across the 47k) -> C44/C43 10uF -> R62/R61 100k to ground -> R54/R52 1k -> PH6/PH5.
// MODELLED:
//   (1) the digital filter, as it appears at the output rate: only its passband shape lies below fs/2.
//       A 35-tap linear-phase FIR, minimax-designed: +-0.059 dB to 0.4535 fs, exactly -6.02 dB at fs/2.
//   (2) the two coupling high-passes whose resistor is on the drawing: 10uF/22k (0.72 Hz) and
//       10uF/100k (0.16 Hz). Together they take every DC component out of the output - the hardware
//       cannot pass DC to its jacks, and until now the emulator did.
// STATED, not read: (a) the FIR delays 17 samples, not 14.7 - a linear-phase filter running at fs
//   cannot be shorter and meet the ripple (+48 us); (b) C126/C118 into the Master VR is not modelled,
//   the pot's value is not on the drawing; (c) the jack's external load is not modelled (100k only);
//   (d) the 22pF/47k pole (154 kHz) and the x2 stage gain belong to the volume/level round, not here;
//   (e) the FIR output is clamped to the 24-bit range.
// ===========================================================================
class Ak4522Dac {
public:
    explicit Ak4522Dac(double fs = 48000.0) { setRate(fs); }
    void setRate(double fs)
    {
        m_a1 = hpf(22.0e3 * 10.0e-6, fs);                              // C134/C117 into R158/R135
        m_a2 = hpf(100.0e3 * 10.0e-6, fs);                             // C44/C43 into R62/R61
    }
    void process(int32_t& l, int32_t& r) { l = channel(0, l); r = channel(1, r); }
private:
    static constexpr int kTaps = 35, kHalf = 17;
    static double hpf(double rc, double fs) { return rc / (rc + 1.0 / fs); }   // y = a (y + x - x1)
    int32_t channel(int c, int32_t s)
    {
        static constexpr double h[kHalf + 1] = {
        0.004930179357, -0.003549788894, 0.004714749311, -0.006035640213, 0.007496955420, -0.009075953980,
        0.010745321002, -0.012470803149, 0.014213801227, -0.015933185813, 0.017585899444, -0.019127009236,
        0.020514891070, -0.021711714331, 0.022680324934, -0.023394272761, 0.023830553103, 0.976022086490 };
        double* x = m_x[c];
        m_p[c] = (m_p[c] + kTaps - 1) % kTaps;                         // newest sample at m_p
        x[m_p[c]] = double(s);
        auto at = [&](int k) { return x[(m_p[c] + k) % kTaps]; };      // k samples ago
        double y = h[kHalf] * at(kHalf);
        for (int k = 0; k < kHalf; ++k) y += h[k] * (at(k) + at(kTaps - 1 - k));
        if (y > 8388607.0) y = 8388607.0; else if (y < -8388608.0) y = -8388608.0;   // STATED (e)
        double& y1 = m_y1[c]; double& x1 = m_x1[c]; double& y2 = m_y2[c]; double& x2 = m_x2[c];
        y1 = m_a1 * (y1 + y - x1);  x1 = y;                            // 0.72 Hz
        y2 = m_a2 * (y2 + y1 - x2); x2 = y1;                           // 0.16 Hz
        return int32_t(std::lround(y2));
    }
    double m_a1 = 0.0, m_a2 = 0.0;
    double m_x[2][kTaps] = {};
    int    m_p[2] = {};
    double m_x1[2] = {}, m_y1[2] = {}, m_x2[2] = {}, m_y2[2] = {};
};

} // namespace MS2000
