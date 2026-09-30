# ms2000r-emu

A hardware-level emulator of the **KORG MS2000R** (rack version of the MS2000 analog-modeling synthesizer):
the H8S/2350 main CPU runs the original firmware instruction by instruction, and the DSP56362 runs the
original DSP code on the dsp56300 emulator library. Nothing of the synth engine is re-implemented - the sound
comes from the machine's own code.

Standalone application (Windows, ImGui) with the full MS2000R front panel, and a VST3 instrument plugin (JUCE).

## Status

- The firmware boots and runs: programs, the vocoder, the arpeggiator, the motion/mod sequencer, the demo songs,
  MIDI IN and MIDI OUT (knob moves send CCs), Global and program WRITE (the flash is emulated, non-volatile).
- Front panel: all 32 knobs, the switch matrix, the LED matrix (as the firmware multiplexes it) and the 16x2 LCD
  including its custom characters. Shift+click on a program key 1-16 or EXIT keeps it held (chords, EXIT+GLOBAL).
- Audio In 1/2 (vocoder, OSC1 AUDIO IN) through a model of the input stage and the AK4522 codec.
- VST3 (phase 2): the panel in the plugin window; the machine's state (knob positions, settings and the written
  flash sectors) is saved in the host project.

Known limits: Windows only; one plugin instance per process; the plugin needs a host buffer of about 256 samples
(128 is too little for now); at 96 kHz the plugin crackles; old hosts (VSTHost) can hang when leaving the demo
songs. The emulator needs a recent CPU - it runs about 2x real time on a current desktop.

## What you need - NOT included

This repository contains **no firmware, no ROM and no manual**. You need dumps from your own hardware:

| file (in the working folder) | what | required |
|---|---|---|
| `flash.bin` | the MS2000R main flash (1 MB, Am29LV800B) | yes |
| `full FW/boot-362.ms2000.bin` | the DSP56362 boot ROM (768 bytes = 192 words x 4, little endian) | yes (no sound without it) |
| `hd44780_a00.bin` | the LCD controller's character generator ROM (4096 bytes) | no (the LCD falls back to text) |

The emulator never writes `flash.bin`: what the firmware writes (WRITE) goes to `ms2000_flash_state.bin` /
`.sectors` beside it (standalone) or into the host project (plugin).

## Build (Windows)

Visual Studio 2022, CMake 3.16+, [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set (ImGui comes
from `vcpkg.json`).

```
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target ms2000_emulator
build\Release\ms2000_emulator.exe --gui
```

VST3 plugin (JUCE is fetched at configure time):

```
cmake -S . -B build_vst -G "Visual Studio 17 2022" -DMS2K_BUILD_VST3=ON
cmake --build build_vst --config Release --target MS2000R_VST_VST3
```

The plugin looks for the folder holding `flash.bin` in `MS2K_HOME`, then `Documents\MS2000R`, then the folders
above the plugin binary.

## Credits

- Written by **Claude** (Anthropic's AI), in a long collaboration with **Tamás Bársony (Tuth)**, who owns the
  hardware, directed the work, tested it by ear against the real unit and supplied the photos, manuals and dumps
  it was measured against.
- The DSP56300 emulation is **dsp56300 by The Usual Suspects (TUS)** from the
  [gearmulator](https://github.com/dsp56300/gearmulator) project - without it there would be no sound. A patched
  copy is in `external/dsp56300`; the changes are in `patches/dsp56300`.
- Thanks to **Unknown Technologies** for the help, the discussions and the bug reports.
- JUCE (via the dsp56300/JUCE fork) for the plugin, Dear ImGui for the standalone GUI.

## License

GPL-3.0 (see `LICENSE.md`), as the dsp56300 library it is built on.

KORG and MS2000 are trademarks of KORG Inc. This project is not affiliated with or endorsed by KORG.
