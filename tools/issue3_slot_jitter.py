#!/usr/bin/env python3
"""ms2000r-emu issue #3 detector: is one output channel jittering by one sample against the other?

Usage:  python tools/issue3_slot_jitter.py recording.wav [t0 t1]

Works on any 16/24/32-bit or float stereo WAV in which L and R carry the SAME signal (a centre-panned
note, Panpot = CNT). For every sample where the signal moves enough to tell, it decides whether R
matches L[n] (on time) or L[n-1] (one sample late), and does the same the other way round.

A clean render gives ~0 % late for both channels. The v0.9.6 bug recording
(bug_dist_1center_2left_3right.wav, centre note 1.30-2.45 s) gives:
    R one sample late 67.5 %, L 0.1 %, 3830 episodes, median 4 samples,
    296 exact R repeats while L moves, R +10.9 dB above L at 6-12 kHz, +13.1 dB at 12-24 kHz.
Exit code 1 if either channel is late in more than 5 % of the judged samples.
"""
import sys
import numpy as np
from scipy.io import wavfile

def main():
    sr, x = wavfile.read(sys.argv[1])
    if x.ndim != 2 or x.shape[1] < 2:
        sys.exit("need a stereo file")
    x = x[:, :2].astype(np.float64)
    if np.abs(x).max() <= 1.0:
        x *= 32768.0                       # float file: work in 16-bit LSB units
    elif np.abs(x).max() > 40000:
        x /= np.abs(x).max() / 32768.0     # 24/32-bit int: scale to 16-bit LSB units
    if len(sys.argv) >= 4:
        a, b = int(float(sys.argv[2]) * sr), int(float(sys.argv[3]) * sr)
        x = x[a:b]
    L, R = x[:, 0], x[:, 1]
    both = (np.abs(L) > 8) & (np.abs(R) > 8)
    if both.sum() < sr * 0.05:
        sys.exit("L and R are not both active long enough - use a centre-panned note")

    def late(a, b):  # is b one sample late against a?
        on, one = np.abs(b[2:] - a[2:]), np.abs(b[2:] - a[1:-1])
        move = (np.abs(a[2:] - a[1:-1]) > 30) & both[2:]
        flags = (one < on) & move
        return flags, move.sum()

    # The two channels must be the same signal up to a one-sample shift: the smallest L-R residual over
    # the lags -1/0/+1 must be well below the signal (a program with its own stereo spread is ~-20 dB
    # at every lag, and the one-sample test is a coin toss there).
    e = np.mean(L[both] ** 2)
    res = min(np.mean((R[1:] - L[1:]) ** 2), np.mean((R[1:] - L[:-1]) ** 2), np.mean((R[:-1] - L[1:]) ** 2))
    if 10 * np.log10(res / e + 1e-30) > -27:
        sys.exit(f"L and R are not the same signal (best L-R residual {10*np.log10(res/e+1e-30):.1f} dB) - "
                 "stereo effects or panning; this test needs a dry, centre-panned note")
    rl, judged = late(L, R)
    ll, _ = late(R, L)
    pr, pl = 100 * rl.sum() / max(judged, 1), 100 * ll.sum() / max(judged, 1)
    runs, cur = [], 0
    for v in rl:
        if v: cur += 1
        elif cur: runs.append(cur); cur = 0
    rep = int(((R[1:] == R[:-1]) & (np.abs(L[1:] - L[:-1]) > 30) & both[1:]).sum())

    def band(v, lo, hi):
        S = np.abs(np.fft.rfft(v * np.hanning(len(v)))) ** 2
        f = np.fft.rfftfreq(len(v), 1 / sr)
        return 10 * np.log10(S[(f >= lo) & (f < hi)].sum() + 1e-20)

    print(f"judged samples      {judged}")
    print(f"R one sample late   {pr:5.1f} %   ({len(runs)} episodes, median {np.median(runs) if runs else 0:.0f} samples)")
    print(f"L one sample late   {pl:5.1f} %")
    print(f"R exact repeats while L moves: {rep}")
    for lo, hi in [(3000, 6000), (6000, 12000), (12000, 24000)]:
        if hi <= sr / 2 + 1:
            print(f"R - L energy {lo:5d}-{hi:5d} Hz: {band(R[both], lo, hi) - band(L[both], lo, hi):+6.1f} dB")
    bad = pr > 5 or pl > 5
    print("RESULT:", "SLOT JITTER (issue #3 signature)" if bad else "clean")
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
