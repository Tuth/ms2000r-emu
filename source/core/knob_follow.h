// knob_follow.h - KNOB-FOLLOW (2026-10-01): where each front-panel pot stands for the program being edited.
//
// The edit buffer is the 254-byte program (MIDI Implementation TABLE 1) at DRAM 0x402E0E - measured: the whole
// A01 program sits there byte-identical to the firmware's own 4C dump, and moving CUTOFF changes +58 there
// (TIMBRE1 +20) while the stored copy at 0x402F0C stays (CLAUDE.md "KNOB-FOLLOW step 1"). This maps a program to
// pot positions (0..1023, the pots' own scale) for display: a pot shows the parameter its primary function edits,
// for the timbre being edited. Pots whose meaning depends on state this does not read (the FX pair: mod or delay)
// and the vocoder layout are left out (followed = false: they show the physical pot).
#pragma once
#include <cstdint>

namespace MS2000 {

constexpr uint32_t kEditBufferAddr = 0x402E0Eu;

// TABLE 2 offsets of a timbre (TIMBRE1 = 38, TIMBRE2 = 146) for each pot [mux][x] (KOD-A30415 IC1..IC4 = mux 0..3),
// -1 = not a timbre parameter. Pot -> function as vector_panel.h draws them.
inline void programKnobs(const uint8_t* p /* 254 bytes */, int timbre, uint16_t out[4][8], bool followed[4][8])
{
    for (int m = 0; m < 4; ++m) for (int x = 0; x < 8; ++x) followed[m][x] = false;
    const int voiceMode = (p[16] >> 4) & 3;                         // 0..3 = Single, Split, Layer, Vocoder
    if (voiceMode == 3) return;
    const int T = 38 + 108 * (voiceMode == 0 ? 0 : (timbre ? 1 : 0));
    auto lin = [](int v, int lo, int hi) {                          // a value in [lo, hi] -> 0..1023
        if (v < lo) v = lo; if (v > hi) v = hi;
        return uint16_t((uint32_t(v - lo) * 1023u + uint32_t(hi - lo) / 2u) / uint32_t(hi - lo));
    };
    auto set = [&](int m, int x, uint16_t v) { out[m][x] = v; followed[m][x] = true; };
    auto byte7 = [&](int m, int x, int off) { set(m, x, lin(p[T + off] & 0x7F, 0, 127)); };
    // OSCILLATOR 1 / 2, MIXER
    byte7(2, 6, 8);  byte7(2, 1, 9);                                // CONTROL 1 / 2
    set(2, 7, lin(p[T + 13], 64 - 24, 64 + 24));                    // SEMITONE 64 +/- 24
    byte7(2, 0, 14);                                                // TUNE 64 +/- 63
    byte7(2, 2, 16); byte7(2, 4, 17); byte7(2, 5, 18);              // OSC1 / OSC2 / NOISE level
    // FILTER, AMP, PORTAMENTO
    byte7(3, 7, 20); byte7(3, 2, 21); byte7(3, 4, 22); byte7(3, 6, 24);   // CUTOFF RESO EG1 INT KBD TRACK
    byte7(3, 3, 25); byte7(3, 1, 26);                               // LEVEL PAN
    byte7(2, 3, 15);                                                // PORTAMENTO (+15 B0..6)
    // EG 1 / EG 2 (A D S R)
    byte7(0, 4, 30); byte7(0, 6, 31); byte7(0, 2, 32); byte7(0, 0, 33);
    byte7(0, 7, 34); byte7(0, 1, 35); byte7(0, 5, 36); byte7(0, 3, 37);
    // LFO 1 / 2 frequency (with Tempo Sync on the same pot picks a note - the byte is still the pot's value)
    byte7(1, 4, 39); byte7(1, 6, 42);
    // VIRTUAL PATCH 1..4 intensity
    byte7(1, 7, 45); byte7(1, 0, 47); byte7(1, 3, 49); byte7(1, 1, 51);
    // ARPEGGIATOR (program common): TEMPO 20..300 (bytes 30 MSB, 31 LSB), GATE 0..100 %
    set(3, 0, lin((int(p[30]) << 8) | p[31], 20, 300));
    set(3, 5, lin(p[34], 0, 100));
}

} // namespace MS2000
