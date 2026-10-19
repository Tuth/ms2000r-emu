# ms2000r-emu  (v0.9.8)

A hardware-level emulator of the **KORG MS2000R** (rack version of the MS2000 analog-modeling synthesizer):
the H8S/2350 main CPU runs the original firmware instruction by instruction, and the DSP56362 runs the
original DSP code on the dsp56300 emulator library. Nothing of the synth engine is re-implemented - the sound
comes from the machine's own code.

Standalone application (Windows and macOS, ImGui) with the full MS2000R front panel, and a VST3 and CLAP instrument plugin (JUCE; on macOS also an Audio Unit).

## Status

- The firmware boots and runs: programs, the vocoder, the arpeggiator, the motion/mod sequencer, the demo songs,
  MIDI IN and MIDI OUT (knob moves send CCs), Global and program WRITE (the flash is emulated, non-volatile).
- Front panel: all 32 knobs, the switch matrix, the LED matrix (as the firmware multiplexes it) and the 16x2 LCD
  including its custom characters. Shift+click on any key keeps it held until clicked again (chords, EXIT+GLOBAL,
  the service manual's power-on key combinations).
- Output: the AK4522 DAC's digital filter and the line-output coupling (KOD-A30413), and the POWER/VOLUME pot as the
  real 10 kOhm linear Master VR loaded by the next stage. At its minimum the pot's switch turns the machine off
  (dark panel, the flash is kept); turning it up is a cold boot with the held keys down. Plugin Settings can disable
  the switch, so a DAW fade to 0 keeps the machine running. The standalone has the same switch (its VOLUME pot or
  slider) and the same pot law. An Output boost of 0..+12 dB (1 dB steps) after the pot: plugin Settings, standalone
  slider - at 0 dB the output is unchanged.
- DAW automation (plugin): 59 host parameters, each one a PHYSICAL control - the 32 pots, POWER/VOLUME, AUDIO IN 1/2,
  IN 2 MIC/LINE and 23 sound/performance keys (momentary, 1 = held). An automated pot is a hand on the pot: the
  firmware reads it and sends its own CCs as on the unit; the plugin never makes MIDI from a parameter, and nothing
  the machine does moves a parameter (the pots are not motorised) - so automation and MIDI CC tracks never fight.
  Mouse moves on the panel are recorded in Write/Touch mode.
- **MS2000 Flash Tool** (Windows, in the release zip): reads the complete 1 MB flash of your own MS2000 / MS2000R over
  MIDI - the `flash.bin` this emulator needs - with unknown-technologies' flash dump, installed through the unit's own
  update mode from KORG's v1.07 updater file. **Never with an MS2000B / MS2000BR.** See
  [source/tools/flashdump/README.md](source/tools/flashdump/README.md) and [docs/ipl_protocol.md](docs/ipl_protocol.md).
- Audio In 1/2 (vocoder, OSC1 AUDIO IN) through a model of the input stage and the AK4522 codec.
- .syx: load a bank or program file into the machine and save all 128 programs as .syx (standalone: the buttons
  under the panel; plugin: Settings). The file goes into the emulated MIDI IN and the firmware itself stores it,
  as the real unit does from an editor; its DATA LOAD COMPLETED / ERROR answer is shown.
- Plugin extras: a Library page (open a .syx, click a program to hear it - it goes into the edit buffer, the memory is
  not changed); the host tempo as MIDI clock and the host transport as Start/Stop (GLOBAL: MIDI Clock = Ext, the
  arpeggiator starts on beat 1 with the host); Settings -> "Choose the MS2000 folder...".
- "Knobs show the program" (plugin Settings, standalone checkbox): the pots are drawn where the program being
  edited has its values (read from the machine's edit buffer); turning one takes over from there without a jump.
- VST3 (phase 2): the panel in the plugin window; the machine's state (knob positions, settings and the written
  flash sectors) is saved in the host project.
- Several plugin instances in one host: the plugin bundle carries the engine as
  `Contents\Resources\MS2000R_engine.dll`; every instance after the first runs from its own copy of it in
  `%TEMP%\MS2000R_engines` (removed when the host unloads the plugin), so each instance is a separate machine.
- CLAP: the same plugin (clap-juce-extensions), with the same parameters, state and several instances per host (the
  engine copies go to `MS2000R_clap_engines` in the temp folder).

Known limits: the GUI and the plugin run on Windows and macOS (VST3 and CLAP, and AU on macOS); Linux has a headless build (see below). VSTHost stops calling a plugin that sends SysEx to it (e.g. on leaving the demo songs):
switch off "MIDI OUT SysEx -> host" in the plugin's Settings there. The emulator needs a recent CPU - it runs about 2x real time on a current desktop; the plugin runs at
96 kHz with a 128-sample buffer (measured: 0.62 ms median, 0.96 ms worst per 1.33 ms block after the boot).
Next: a preset library page.

## Download

Windows builds are on the [Releases](../../releases) page: `ms2000_emulator.exe` with its two launchers and the
`MS2000R.vst3` plugin. Put your ROM files (below) beside the exe, or in a folder above it, and start
`MS2000R.bat` (or `MS2000R (log).bat` to get `ms2000_gui.log` for a bug report). Copy `MS2000R.vst3` to your
VST3 folder (usually `C:\Program Files\Common Files\VST3`); on its first start open Settings -> "Choose the
MS2000 folder..." and pick the folder with the ROMs (remembered in `%APPDATA%\MS2000R\home.txt`). The Microsoft Visual C++ 2015-2022 x64 runtime is needed.

macOS (Apple Silicon only, arm64) builds are in this repository, in `dist/macOS`: the standalone `MS2000R.app` and the
plugins `MS2000R.vst3`, `MS2000R.component` (AU) and `MS2000R.clap`, built from the commit that added them (see
[Build (macOS)](#build-macos) to build your own). Install the plugins with

```
cp -R dist/macOS/MS2000R.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R dist/macOS/MS2000R.component ~/Library/Audio/Plug-Ins/Components/
cp -R dist/macOS/MS2000R.clap ~/Library/Audio/Plug-Ins/CLAP/
```

and put `MS2000R.app` anywhere; the ROM folder is found or asked for as described under Build (macOS).

They are ad-hoc signed, which is bound to this Mac's copy of the files: a download through a browser (quarantine), a
Finder/iCloud attribute or any changed file makes macOS refuse them ("damaged", or a plugin the host does not list).
Sign them again locally - no Apple account needed - in the folder that holds them (`dist/macOS`, or each plugin
folder after installing):

```
xattr -cr MS2000R.app MS2000R.vst3 MS2000R.component MS2000R.clap
codesign --force --sign - MS2000R.app
for b in MS2000R.vst3 MS2000R.component MS2000R.clap; do
  codesign --force --sign - $b/Contents/Resources/MS2000R_engine   # the engine first: it is code outside Contents/MacOS
  codesign --force --sign - $b
done
codesign -v --deep --strict MS2000R.app MS2000R.vst3 MS2000R.component MS2000R.clap   # silent = all valid
```

`xattr -cr` removes the quarantine and the attributes codesign refuses ("resource fork, Finder information, or
similar detritus not allowed"). For the AU, make macOS rescan and check it: `killall -9 AudioComponentRegistrar`, then
`auval -v aumu Ms2r Msem` (ends with "AU VALIDATION SUCCEEDED"). An ad-hoc signature is for this Mac only; to pass
the files on to other Macs they need a Developer ID signature and notarization.

## What you need - NOT included

This repository contains **no firmware, no ROM and no manual**. You need dumps from your own hardware:

The working folder is found by itself: `MS2K_HOME` if set, else the current folder if it holds the two required ROMs,
else the exe's (or the plugin's) folder or the first folder above it that holds them. Settings (`thin_gui.ini`), the
machine's memory and `recordings\` are kept there.

| file (in the working folder) | what | required |
|---|---|---|
| `flash.bin` | the MS2000R main flash (1 MB, MBM29LV800BA) | yes |
| `boot-362.bin` | the DSP56362 boot ROM (768 bytes = 192 words x 4, little endian), **beside `flash.bin` or in its `full FW` subfolder** (both work since v0.9.8); the widely shared dump works - its two known bad words are corrected in memory. A corrected `boot-362.ms2000.bin` is used first if present | yes (no sound without it) |
| `hd44780_a00.bin` | the LCD controller's character generator ROM (4096 bytes), beside `flash.bin` or in `full FW` | no (the LCD falls back to text) |

If something is missing, the console / the plugin's panel names the missing file and the folder it looked in.
The release zip has a `ROMS - READ ME.txt` with the same instructions.

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

`dist\MS2000R.bat` and `dist\MS2000R (log).bat` are the launchers to put beside the exe.

## Build (Linux, headless)

The standalone builds headless with GCC 13 or Clang (no GUI, no plugin): CMake 3.16+, Ninja, and the ALSA headers
(`libasound2-dev`). The CPU needs AVX2, as on Windows.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ms2000_emulator
cd <folder with the ROMs> && <repo>/build/ms2000_emulator --headless --selftest-full
MS2K_MIDIIN="2:C008;2.3:903C64;3.8:803C00" <repo>/build/ms2000_emulator --headless --quiet-boot --duration 6 --render-test out.wav
```

`MS2K_MIDIIN` schedules MIDI IN bytes at MCU seconds (`t:hex;t:hex`); `--render-test` writes the DSP's frames
as a 32-bit WAV. `ctest` in the build folder runs the test suite.

## Build (macOS)

Apple Silicon builds natively (the DSP runs on the dsp56300 library's ARM64 JIT; its output is bit-identical to the
x64 build). Xcode Command Line Tools, CMake 3.16+ and Ninja (`brew install cmake ninja`); ImGui and GLFW are fetched
at configure time. If the repository is in a folder iCloud syncs (Desktop, Documents), give the build folder a name
ending in `.nosync`: iCloud makes "name 2" copies of files a build rewrites, and the build then fails on them.

```
cmake -S . -B build.nosync -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build.nosync --target MS2000R_app
```

`build.nosync/MS2000R.app` is the standalone with the front panel (GLFW + Metal). Put it in the folder with the ROMs, or
anywhere - on its first start it asks for that folder and remembers it
(`~/Library/Application Support/MS2000R/home.txt`); `~/Documents/MS2000R` is found by itself. Its output goes to
`ms2000_gui.log` there. From a terminal the same executable runs headless:

```
cd <folder with the ROMs> && <repo>/build.nosync/ms2000_emulator --headless --selftest-full
<repo>/build.nosync/ms2000_emulator --gui
<repo>/build.nosync/ms2000_emulator --list-midi
<repo>/build.nosync/ms2000_emulator --play --midi-in "<part of a MIDI port's name>" --midi-out "<part of a name>"
```

`--play` runs the machine on the default audio output without the panel (`--audio-device <name>` picks another,
`--volume 0..1` is the VOLUME pot, default 0.7) until Ctrl+C; every panel pot stands at its centre. The firmware
boots in about 3 s, muted - a note sent before that is not heard (the `MS2K_MIDIIN` example above plays during the
boot; use `4.0:903C64;6.0:803C00` with `--duration 8`). AUDIO IN 1/2 (vocoder, OSC1 AUDIO IN) are chosen in the GUI: an
interface with more than two inputs is listed as stereo pairs ("IN 3-4"), and macOS asks once for the microphone
permission.
`-DCMAKE_OSX_ARCHITECTURES=x86_64` builds the x64 version, which runs under Rosetta 2.

The plugin, VST3, CLAP and Audio Unit (JUCE and clap-juce-extensions are fetched at configure time - CMake 3.21+ for
the CLAP, `-DMS2K_BUILD_CLAP=OFF` leaves it out; the pinned JUCE is patched for the macOS 15+ SDK,
`cmake/macos/juce_patch.cmake`):

```
cmake -S . -B build_vst.nosync -G Ninja -DCMAKE_BUILD_TYPE=Release -DMS2K_BUILD_VST3=ON
cmake --build build_vst.nosync --target MS2000R_Shim MS2000R_AUShim MS2000R_ClapShim
cp -R build_vst.nosync/VST3_multi/MS2000R.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R build_vst.nosync/AU_multi/MS2000R.component ~/Library/Audio/Plug-Ins/Components/
cp -R build_vst.nosync/CLAP_multi/MS2000R.clap ~/Library/Audio/Plug-Ins/CLAP/
```

All three bundles carry a loader module and the engine, as on Windows: every instance after the first runs from its own
copy of the engine in `$TMPDIR`, so several instances in one host are separate machines (AU: Logic, GarageBand).
They are ad-hoc signed - fine for your own builds; to pass them to other Macs they need a Developer ID signature
and notarization. The plugin finds the MS2000 folder as on Windows (`MS2K_HOME`, the folder chosen in Settings,
`~/Documents/MS2000R`, the folders above the plugin).

VST3 and CLAP plugin (JUCE and clap-juce-extensions are fetched at configure time):

```
cmake -S . -B build_vst -G "Visual Studio 17 2022" -DMS2K_BUILD_VST3=ON
cmake --build build_vst --config Release --target MS2000R_Shim MS2000R_ClapShim
```

The finished plugin bundle is `build_vst\VST3_multi\MS2000R.vst3` (the loader module plus the engine). The CLAP is
the folder `build_vst\CLAP_multi\MS2000R` (`MS2000R.clap`, the loader module, and `MS2000R_engine.dll` beside it):
copy the whole folder to `%COMMONPROGRAMFILES%\CLAP`.

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
