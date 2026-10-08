// MVR-1: Master VR = ALPS RK0971221Z05 (KOD-A30413; parts list p.23 "W/SW", no value), 10 kOhm LINEAR, 2 sections,
// with switch. Its wiper drives C134/C117 into R158/R135 22k (at audio frequencies the 10uF is a short), so the
// divider is loaded:  g(x) = x*RL / (RL + x*(1-x)*R),  R = 10k, RL = 22k:  g(1) = 1, g(0.5) = -6.96 dB, g(0.25) = -12.75 dB.
// The switch section (PSW1/PSW2) breaks the power at the minimum (PWR-SW-1) - the plugin and the standalone do that.
#pragma once

inline float ms2kMasterVrGain(float x)
{
    constexpr double R = 10.0e3, RL = 22.0e3;
    const double p = x < 0.0f ? 0.0 : x > 1.0f ? 1.0 : double(x);
    return float(p * RL / (RL + p * (1.0 - p) * R));
}
#include <cmath>
// OUT-BOOST-1: the master output boost (plugin Settings / standalone), 0..+12 dB in 1 dB steps, after the pot.
inline float ms2kBoostGain(int db) { return std::pow(10.0f, float(db < 0 ? 0 : db > 12 ? 12 : db) / 20.0f); }
inline bool ms2kPowerSwitchOpen(float volume) { return volume <= 0.001f; }   // the switch section at the minimum
