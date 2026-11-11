# Changelog

## 2026-10-24

### Added

- Add the MS2000R app icon source files (`icon/KorgMS2000_Icon.png` and its
  editable XCF) and integrate the icon into the macOS app bundle. The build
  generates the required ICNS representations, places the icon in the bundle's
  Resources directory, and declares it in `Info.plist`.

### Changed

- Improve macOS and Apple Clang build compatibility:
  - Replace deprecated `-Ofast` with `-O3 -ffast-math` for Apple release builds.
  - Apply unused-warning suppressions to C++ and Objective-C++ builds, mark
    third-party DSP/asmjit include directories as system headers, and suppress
    the Apple Clang diagnostic emitted by the H8S variadic logging macros.
  - Restrict asmjit compiler flags to compatible toolchains and configure its
    Apple Clang-specific warning options.
  - Patch the fetched ImGui Metal backend to use shared texture storage rather
    than deprecated managed storage.
- Make DSP/JIT code more portable and robust:
  - Include MSVC intrinsics only when compiling with MSVC.
  - Store an explicit JIT pointer in `DspRegs` instead of deriving it using
    offsets within `DSP`, avoiding assumptions about non-standard-layout
    object layout.
  - Skip the generated-code diagnostic dump when the optional
    `execUntilCycles` function pointer is null.
  - Rewrite negative signed left shifts in transfer-limit calculations to
    avoid undefined behavior while preserving the intended limit.
  - Initialize and reset asmjit allocator members explicitly rather than
    clearing whole objects with `memset`; use defaulted memberwise copy
    operations for affected allocator types.
  - Use bounded formatting for DSP disassembly, profiling, and diagnostic
    strings, and correct format argument types.
- Clean up compiler warnings in the emulator:
  - Match constructor initializer order to member declaration order and
    explicitly initialize omitted aggregate fields.
  - Use the shared cross-compiler `__forceinline` spelling for H8S helpers.
  - Add explicit fallback handling to DSP/JIT switches and ensure instruction
    execution returns a failure result on its fall-through path.
  - Use `std::size_t` for string indexes and comparisons with string lengths.
  - Correct diagnostic format specifiers and casts.
- Correct LCD handling:
  - Fix the Entry Mode Set mask so the observer recognizes all combinations of
    cursor direction and display-shift settings.
  - Use actual null-character terminators for the runner's LCD buffers and
    explicitly initialize the nested LED-state array.
  - Adjust diagnostic message buffers for several longer LCD logs.
- Continue LCD-path debugging:
  - Load `hd44780_a00.bin` from the firmware directory captured before macOS
    window initialization changes the process working directory. This fixes the
    false “LCD drawn as text” warning when the ROM is present in the selected
    MS2000 folder.
  - Initialize and reset the GUI LCD buffer to blank DDRAM, with the display
    off and custom-character RAM cleared, preventing zero-filled state from
    appearing as custom glyphs and avoiding stale LCD contents across boots.
  - Forward firmware-decoded GPIO LCD commands and data to `RealLCDDisplay` as
    well as the GUI buffer, keeping the runner's text API and GUI display state
    synchronized.
  - Disable synthetic initialization, test text, stuck-firmware recovery, and
    timeout assistance in the legacy H8S LCD adapter while preserving comments
    that describe the optional fallback behavior.

### Validation

- The macOS `MS2000R_app` target builds successfully with the generated ICNS
  resource included, and the resulting bundle signature verifies.
- The AU and VST plugins have both been tested and are working.
- The macOS `MS2000R_app` target builds after the LCD-path updates, and the
  focused LCD GUI buffer test passes, including its blank power-on state check.
- The saved standalone home file resolves to the firmware folder and
  `hd44780_a00.bin` is present there at 4096 bytes. The missing-character-ROM
  warning was traced to loading the ROM after macOS window initialization
  changed the working directory, and is fixed by loading from the captured
  firmware directory.
- The DSP test runner builds, but the `dsp56300_unitTests` suite still fails in
  `blockOnExtensionWord`; that separate overlapping-JIT-block issue is not
  resolved by these changes.
