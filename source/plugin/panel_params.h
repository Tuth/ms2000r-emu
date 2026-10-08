// AUTOMATION-1 (2026-10-08): the host parameters are the PHYSICAL panel controls (Tamas: "fizikai vonalon").
// An automated parameter is a hand on the pot / a finger on the key: the firmware reads it through the ADC / switch
// matrix exactly as on the unit and does everything else itself - including the CCs it sends on MIDI OUT (GLOBAL
// decides that, as on the hardware). The plugin never generates MIDI from a parameter, and nothing the machine does
// (program change, an incoming CC) moves a parameter: the pots are not motorised. So host automation and MIDI CC
// tracks never feed back into each other.
// Keys are momentary, as the real ones: 1 = held down. Left out on purpose: the program pads (MIDI does programs
// and notes), WRITE / EDIT / GLOBAL / EXIT / CURSOR / PAGE / YES / NO (editing and flash writes are not automation).
// The ids are part of every saved project - never change or reuse one.
#pragma once
#include <cstdint>

namespace ms2kparams {
enum Kind : uint8_t { kKnob, kKey, kVolume, kIn1, kIn2, kMic };
struct Param { const char* id; const char* name; Kind kind; uint8_t a, b; };   // knob: mux, x; key: column, row

static constexpr Param kParams[] = {
    { "o1c1",   "OSC1 Control 1",       kKnob, 2, 6 }, { "o1c2",   "OSC1 Control 2",       kKnob, 2, 1 },
    { "o2semi", "OSC2 Semitone",        kKnob, 2, 7 }, { "o2tune", "OSC2 Tune",            kKnob, 2, 0 },
    { "mx1",    "Mixer OSC1",           kKnob, 2, 2 }, { "mx2",    "Mixer OSC2",           kKnob, 2, 4 },
    { "mxn",    "Mixer Noise",          kKnob, 2, 5 },
    { "cut",    "Filter Cutoff",        kKnob, 3, 7 }, { "res",    "Filter Resonance",     kKnob, 3, 2 },
    { "eg1int", "Filter EG1 Int",       kKnob, 3, 4 }, { "kbdtr",  "Filter Kbd Track",     kKnob, 3, 6 },
    { "lvl",    "Amp Level",            kKnob, 3, 3 }, { "pan",    "Amp Pan",              kKnob, 3, 1 },
    { "eg1a",   "EG1 Attack",           kKnob, 0, 4 }, { "eg1d",   "EG1 Decay",            kKnob, 0, 6 },
    { "eg1s",   "EG1 Sustain",          kKnob, 0, 2 }, { "eg1r",   "EG1 Release",          kKnob, 0, 0 },
    { "eg2a",   "EG2 Attack",           kKnob, 0, 7 }, { "eg2d",   "EG2 Decay",            kKnob, 0, 1 },
    { "eg2s",   "EG2 Sustain",          kKnob, 0, 5 }, { "eg2r",   "EG2 Release",          kKnob, 0, 3 },
    { "lfo1f",  "LFO1 Frequency",       kKnob, 1, 4 }, { "lfo2f",  "LFO2 Frequency",       kKnob, 1, 6 },
    { "fxspd",  "FX Speed / Time",      kKnob, 1, 2 }, { "fxdep",  "FX Depth / Feedback",  kKnob, 1, 5 },
    { "patch1", "Patch 1 Intensity",    kKnob, 1, 7 }, { "patch2", "Patch 2 Intensity",    kKnob, 1, 0 },
    { "patch3", "Patch 3 Intensity",    kKnob, 1, 3 }, { "patch4", "Patch 4 Intensity",    kKnob, 1, 1 },
    { "porta",  "Portamento Time",      kKnob, 2, 3 },
    { "tempo",  "Arp Tempo",            kKnob, 3, 0 }, { "gate",   "Arp Gate",             kKnob, 3, 5 },
    { "vol",    "Power / Volume",       kVolume, 0, 0 },
    { "in1",    "Audio In 1 Level",     kIn1, 0, 0 },  { "in2",    "Audio In 2 Level",     kIn2, 0, 0 },
    { "mic2",   "Audio In 2 Mic/Line",  kMic, 0, 0 },
    { "k_o1wave",  "OSC1 Wave key",              kKey, 0, 7 }, { "k_o2wave", "OSC2 Wave key",        kKey, 1, 1 },
    { "k_o2mod",   "OSC2 Osc Mod key",           kKey, 1, 0 }, { "k_ftype",  "Filter Type key",      kKey, 1, 4 },
    { "k_eg2gate", "EG2/Gate key",               kKey, 1, 7 }, { "k_dist",   "Distortion key",       kKey, 1, 6 },
    { "k_timbre",  "Timbre Select key",          kKey, 1, 2 },
    { "k_mseqon",  "Mod Sequence On/Off key",    kKey, 0, 6 }, { "k_mseqrec", "Mod Sequence Rec key", kKey, 1, 5 },
    { "k_seqsel",  "Seq Edit Select key",        kKey, 0, 3 }, { "k_kbd",    "Keyboard key",         kKey, 0, 4 },
    { "k_bankdn",  "Bank/Octave Down key",       kKey, 2, 7 }, { "k_bankup", "Bank/Octave Up key",   kKey, 2, 6 },
    { "k_arpon",   "Arp On/Off key",             kKey, 2, 4 }, { "k_arplat", "Arp Latch key",        kKey, 2, 3 },
    { "k_arprng",  "Arp Range key",              kKey, 2, 2 }, { "k_arptyp", "Arp Type key",         kKey, 2, 1 },
    { "k_lfo1",    "LFO1 Select key",            kKey, 3, 5 }, { "k_lfo2",   "LFO2 Select key",      kKey, 3, 4 },
    { "k_fxsel",   "Effects Mod/Delay key",      kKey, 3, 3 },
    { "k_vpsel",   "Virtual Patch Select key",   kKey, 3, 2 }, { "k_vpsrc",  "Virtual Patch Source key", kKey, 3, 1 },
    { "k_vpdst",   "Virtual Patch Destination key", kKey, 3, 0 },
};
static constexpr int kCount = int(sizeof kParams / sizeof kParams[0]);
}
