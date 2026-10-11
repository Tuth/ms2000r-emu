cmake_minimum_required(VERSION 3.16)

# Set vcpkg toolchain for ImGui support
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    endif()
endif()

project(KorgMS2000 VERSION 0.9.6 LANGUAGES CXX)
add_compile_definitions(MS2K_VERSION="${PROJECT_VERSION}")   # PUBLIC-1b: shown in the GUI title and the plugin

# Enable testing support for CTest
enable_testing()

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build types
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags (compiler-specific)
if(MSVC)
    # MSVC-compatible flags
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /O2 /Zi /DNDEBUG")
else()
    # GCC/Clang flags
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g -DNDEBUG")
endif()

# Platform specific settings
if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

# DSP56300 submodule
add_subdirectory(external/dsp56300 ${CMAKE_CURRENT_BINARY_DIR}/dsp56300)

# LINUX-1: GCC/Clang warnings for this project's own code only. Set after add_subdirectory so the
# dsp56300 library builds with the flags upstream builds it with (-Wpedantic turns its empty
# g_nonVolatileXMMs[] - System V has no callee-saved XMM - into an error). And the MSVC keywords
# the sources use (__forceinline, fopen_s) come from one force-included header, so no source file
# changes for them.
if(NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
endif()
if(NOT MSVC)
    add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/source/core/platform_compat.h)
    # The sources use 256-bit AVX/AVX2 intrinsics (h8s2350_emulator.h, h8s2350_simd_optimizations).
    # MSVC emits them without a switch; GCC/Clang need the target enabled. Same CPU requirement as
    # the Windows build.
    add_compile_options(-mavx2)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/external/dsp56300/source
    ${CMAKE_CURRENT_SOURCE_DIR}/external/dsp56300/source/dsp56kEmu
    ${CMAKE_CURRENT_SOURCE_DIR}/external/dsp56300/source/dsp56kEmu/include
)

# MS2000 library sources - ESSENTIAL FILES ONLY
set(MS2000_SOURCES
    source/core/syx_parser.cpp
    source/core/dsp56362_emulator.cpp
    source/core/thread_prio_win.cpp
    source/core/h8s2350_emulator.cpp
    source/core/h8s2350_instructions.cpp
    source/core/sampling_profiler.cpp
    source/core/h8s2350_interrupt_system.cpp
    source/core/h8s2350_peripherals.cpp
    source/core/h8s2350_simd_optimizations.cpp
    source/core/vector_table_tracker.cpp
    source/core/replay_logger.cpp
    source/core/audio_output.cpp
    source/core/ms2000_audio_system.cpp
    source/core/ms2000_clock_system.cpp
    source/core/lcd_display.cpp
    source/core/real_lcd_display.cpp
    source/core/h8s_lcd_adapter.cpp
    source/core/ms2k_lcd_adapter.cpp
    # boot6.txt: LCD bootstrap and trace system
    source/core/lcd_bootstrap.cpp
    source/core/lcd_trace.cpp
    # fw15.txt: PRNG seed control and deterministic state
    # deterministic_state.h is header-only
    # panel_recorder.h is header-only
    # fw17.txt: Diagnostic panel system 
    # diag_panel.h is header-only
    # diag_panel_widget.h is header-only
    # fw19.txt: I²S/AK4522 audio pipeline
    source/audio/audio_engine.cpp
    source/core/flash_rom.cpp
    source/core/io_ports.cpp
    source/core/timer_module.cpp
    source/core/sci_module.cpp
    source/core/ad_converter.cpp
    source/core/midi_interface.cpp
    source/core/ms2000_control_interface.cpp
    source/core/ms2000_mp_stub.cpp
    source/core/ms2000_knob_mapping.cpp
    source/core/ms2000_panel_adc.cpp
    source/core/ms2000_switch_matrix.cpp
    source/core/ms2000_led_matrix.cpp
    source/core/ms2000_midi_interface_simple.cpp
    source/core/h8s2350_v2_1.cpp
    source/core/ms2000_hardware_mapping.cpp
    source/core/ms2000_runner.cpp
    # fw21.txt: Crash dump system
    source/core/crash_dump_globals.cpp
    source/core/crash_handlers.cpp
    # fw22.txt: LCD screenshot system
    source/core/lcd_screenshot.cpp
    # fw23.txt: Panel I/O validation system
    source/core/panel_io_validator.cpp
    # fw24.txt: Enhanced LCD validation
    source/core/lcd_observer.cpp
    # FIX18: branch forensics for test linkage
    source/core/branch_forensics.cpp
)

# MS2000 library headers - ESSENTIAL FILES ONLY
set(MS2000_HEADERS
    source/core/syx_parser.h
    source/core/dsp56362_emulator.h
    source/core/h8s2350_emulator.h
    source/core/h8s2350_memory_map.h
    source/core/h8s2350_instructions.h
    source/core/h8s2350_interrupt_system.h
    source/core/h8s2350_peripherals.h
    source/core/vector_table_tracker.h
    source/core/ms2000_audio_system.h
    source/core/ms2000_clock_system.h
    source/core/lcd_display.h
    source/core/real_lcd_display.h
    source/core/h8s_lcd_adapter.h
    # boot6.txt: LCD bootstrap and trace system headers
    source/core/lcd_bootstrap.h
    source/core/lcd_trace.h
    source/core/flash_rom.h
    source/core/io_ports.h
    source/core/timer_module.h
    source/core/sci_module.h
    source/core/ad_converter.h
    source/core/midi_interface.h
    source/core/ms2000_control_interface.h
    source/core/ms2000_mp_stub.h
    source/core/ms2000_knob_mapping.h
    source/core/ms2000_panel_adc.h
    source/core/ms2000_switch_matrix.h
    source/core/ms2000_led_matrix.h
    source/core/ms2000_midi_interface_simple.h
    source/core/h8s2350_v2_1.h
    source/core/ms2000_hardware_mapping.h
    source/core/ms2000_runner.h
    source/core/ms2000_emulator.h
    # fw19.txt: I²S/AK4522 audio pipeline headers
    source/audio/spsc_ring.h
    source/audio/i2s_ak4522.h
    source/audio/dsp_audio_adapter.h
    source/audio/audio_engine.h
    source/audio/audio_thread.h
    # fw20.txt: Async log rate limiting
    source/core/async_log.h
    # fw21.txt: Crash dump system headers
    source/core/crash_dump.h
    source/core/crash_handlers.h
    source/core/crash_minidump.h
    # fw22.txt: LCD screenshot system headers
    source/core/lcd_screenshot.h
    source/core/stb_image_write_simple.h
    # fw23.txt: Panel I/O validation system headers
    source/core/panel_io_validator.h
    # fw24.txt: Enhanced LCD validation headers
    source/core/lcd_observer.h
    source/audio/mmcss_audio.h
)

# MS2000 core library
add_library(ms2000 STATIC ${MS2000_SOURCES} ${MS2000_HEADERS})

# PERF-MCU-4 (2026-10-01): whole-program optimisation for the core. The executor TU calls the
# emulator's readByte/writeByte/sync helpers on every instruction; without LTCG none of those
# cross-TU calls can be inlined. /GS- drops the stack-cookie checks (1.5 % of the MCU profile).
if(MSVC)
    target_compile_options(ms2000 PRIVATE $<$<CONFIG:Release>:/GL /GS->)
    set_property(TARGET ms2000 APPEND PROPERTY STATIC_LIBRARY_OPTIONS $<$<CONFIG:Release>:/LTCG>)
    target_link_options(ms2000 INTERFACE $<$<CONFIG:Release>:/LTCG>)
endif()

# MS2000 library properties
set_target_properties(ms2000 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# MS2000 library dependencies
target_link_libraries(ms2000
    dsp56kEmu
    ${CMAKE_THREAD_LIBS_INIT}
)

# ESSENTIAL EXECUTABLES ONLY (1 total - only ms2000_emulator.exe as requested)

# 1. Main emulator application (Runner-based) 
add_executable(ms2000_emulator
    source/standalone/ms2000_emulator_main.cpp
    source/core/branch_forensics.cpp
)
target_link_libraries(ms2000_emulator ms2000)

# Prevent /OPT:REF from stripping gearmulator in stub mode (~200KB)
if(MSVC)
    target_link_options(ms2000_emulator PRIVATE /OPT:NOREF /MAP)   # PERF-124: map for the sampling profiler
endif()

# fw28.txt: Try to add ImGui Win32/DX11 support for GUI mode
if(WIN32)
    find_package(imgui CONFIG QUIET)
    if(imgui_FOUND)
        # Add GUI files only if ImGui is available
        target_sources(ms2000_emulator PRIVATE
            source/gui/gui_win32_dx11.cpp
            source/gui/panel_widgets.cpp
            source/gui/thin_gui.cpp
            source/audio/host_io_win.cpp
        )
        target_link_libraries(ms2000_emulator imgui::imgui d3d11 dxgi comdlg32)
        target_compile_definitions(ms2000_emulator PRIVATE IMGUI_ENABLED)
        message(STATUS "MS2000 Emulator with ImGui GUI support will be built")
    else()
        message(STATUS "ImGui not found - GUI mode will be disabled, fallback to headless")
        target_compile_definitions(ms2000_emulator PRIVATE GUI_DISABLED)
    endif()
else()
    message(STATUS "Non-Windows platform - GUI mode will be disabled")
    target_compile_definitions(ms2000_emulator PRIVATE GUI_DISABLED)
endif()
message(STATUS "MS2000 Main Emulator will be built")

# DISABLED: Only build ms2000_emulator.exe as requested
# 2. Firmware analyzer tool - DISABLED
# add_executable(firmware_analyzer source/standalone/firmware_analyzer.cpp)
# target_link_libraries(firmware_analyzer ms2000)
# message(STATUS "Firmware Analyzer tool will be built")

# 3. Simple test application - DISABLED
# add_executable(ms2000_test source/standalone/minimal_test.cpp)
# target_link_libraries(ms2000_test ms2000)
# message(STATUS "MS2000 Test application will be built")

# 4. LCD test (for debugging display issues) - DISABLED
# add_executable(lcd_test source/standalone/lcd_test.cpp)
# target_link_libraries(lcd_test ms2000)
# message(STATUS "LCD Test will be built")

# 5. Audio Generation Test (MS2000 synthesizer audio output) - DISABLED
# add_executable(audio_test audio_test.cpp)
# target_link_libraries(audio_test ms2000)
# message(STATUS "MS2000 Audio Test will be built")

# ImGui support for GUI
find_package(imgui CONFIG QUIET)
find_package(glfw3 CONFIG QUIET)
find_package(OpenGL QUIET)

# DISABLED: Only build ms2000_emulator.exe as requested
if(imgui_FOUND AND glfw3_FOUND AND OpenGL_FOUND)
    # 5. Advanced GUI with ImGui (only if dependencies available) - DISABLED
    # add_executable(ms2000_gui source/standalone/ms2000_advanced_simd_techniques.cpp)
    # target_link_libraries(ms2000_gui ms2000 imgui::imgui glfw opengl32)
    # set_target_properties(ms2000_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    # message(STATUS "MS2000 Advanced GUI will be built")
else()
    message(STATUS "ImGui/GLFW not found - Advanced GUI will be skipped")
endif()

# Build options
option(MS2K_EMULATE_MCU "Emulate H8S/2350 MCU" ON)
# DEV-FLASH-1: developer build - the flash is the whole chip image MBM29LV800BA.bin (local only, never in git)
option(MS2K_DEV_FLASH "Developer build: read/write the full flash chip image MBM29LV800BA.bin" OFF)
if(MS2K_DEV_FLASH)
  add_compile_definitions(MS2K_DEV_FLASH=1)
endif()
option(MS2K_HOST_UI "Use host UI instead of MCU" OFF)
option(MS2K_DEBUG "Enable debug output" OFF)

# Compiler definitions
target_compile_definitions(ms2000 PRIVATE
    $<$<BOOL:${MS2K_EMULATE_MCU}>:MS2K_EMULATE_MCU>
    $<$<BOOL:${MS2K_HOST_UI}>:MS2K_HOST_UI>
    $<$<BOOL:${MS2K_DEBUG}>:MS2K_DEBUG>
)

# Platform specific settings
if(WIN32)
    target_compile_definitions(ms2000 PRIVATE _WIN32_WINNT=0x0601)
elseif(APPLE)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    target_link_libraries(ms2000 ${COREAUDIO_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
else()
    find_package(ALSA QUIET)
    find_package(Threads REQUIRED)
    if(ALSA_FOUND)
        target_link_libraries(ms2000 ${ALSA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
        target_include_directories(ms2000 PRIVATE ${ALSA_INCLUDE_DIRS})
    else()
        target_link_libraries(ms2000 ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()

# Install rules - only install the main emulator
install(TARGETS ms2000_emulator
    RUNTIME DESTINATION bin
)

# Documentation install
install(FILES
    README.md
    DESTINATION share/doc/ms2000
)

# 6. Headless main for boot verification - DISABLED (user request)
# add_executable(ms2000_main source/standalone/ms2000_main.cpp)
# target_link_libraries(ms2000_main ms2000)
# message(STATUS "MS2000 Headless Main will be built")

# DISABLED: Only build ms2000_emulator.exe as requested
# 7. ROM vector table scanner for TRAPA debugging - DISABLED
# add_executable(rom_vector_scan source/standalone/rom_vector_scan.cpp)
# target_link_libraries(rom_vector_scan ms2000)
# message(STATUS "ROM Vector Scanner will be built")

# 8. Vector Table Tracker integration test - DISABLED
# add_executable(vector_tracker_test vector_tracker_test.cpp)
# target_link_libraries(vector_tracker_test ms2000)

# fw14.txt: Headless Golden Master test for CI/CD - DISABLED
# add_executable(golden_master_test source/tests/golden_master_test.cpp)
# target_link_libraries(golden_master_test ms2000)

# fw14.txt: CGRAM support test - DISABLED
# add_executable(cgram_test source/tests/cgram_test.cpp)
# target_link_libraries(cgram_test ms2000)

# fw14.txt: LCD Diagnostics Widget test - DISABLED (ImGui dependency issues)
# add_executable(lcd_diagnostics_test source/tests/lcd_diagnostics_test.cpp)
# target_link_libraries(lcd_diagnostics_test ms2000)
# message(STATUS "LCD Diagnostics Test will be built")

# fw14.txt: Panel I/O Protocol test - DISABLED
# add_executable(panel_io_test source/tests/panel_io_test.cpp)
# target_link_libraries(panel_io_test ms2000)

# fw17.txt: Diagnostic panel test program - DISABLED
# add_executable(diag_panel_test source/tests/diag_panel_test.cpp)
# target_link_libraries(diag_panel_test ms2000)

message(STATUS "Vector Table Tracker test will be built")

# Sprint 1.3: RAM vs EXT memory penalty validation test
add_executable(memory_penalty_test source/tests/memory_penalty_test.cpp)
target_link_libraries(memory_penalty_test ms2000)
message(STATUS "Memory penalty test will be built")

# P0 Stabilization: 0x7A ABS24 RAM↔EXT regression unit test
add_executable(test_0x7A_mem_penalty source/tests/test_0x7A_mem_penalty.cpp)
target_link_libraries(test_0x7A_mem_penalty ms2000)
add_test(NAME 0x7A_mem_penalty COMMAND test_0x7A_mem_penalty)
message(STATUS "0x7A ABS24 regression test will be built")

# --- P1.5 Extended Branch Tests QUARANTINED (fictional Bcc16 family) ---
# Moved to source/tests/quarantine/ per R5. Real 16-bit Bcc = 0x58 [cc]0 disp16.
# 0x64-0x6B = OR.W/XOR.W/AND.W/MOV @ERs,Rd/MOV abs (per manual)
# 0x7C-0x7F = bit-manipulation groups (per manual)

# --- SUBX tests (P1.6) ---
add_executable(test_subx source/tests/test_subx.cpp)
target_link_libraries(test_subx ms2000)
add_test(NAME SUBX_matrix COMMAND test_subx)
set_tests_properties(SUBX_matrix PROPERTIES LABELS "arith;P1.6")

add_executable(test_ssot_subx_decode source/tests/test_ssot_subx_decode.cpp)
target_link_libraries(test_ssot_subx_decode ms2000)
add_test(NAME SUBX_ssot_smoke COMMAND test_ssot_subx_decode)
set_tests_properties(SUBX_ssot_smoke PROPERTIES LABELS "ssot;arith;P1.6")

# FIX-GOLD: golden decode gate (R5 manual ground truth) - BLOCKING for decoder changes
add_executable(test_golden_decode_45E0 source/tests/test_golden_decode_45E0.cpp)
target_link_libraries(test_golden_decode_45E0 ms2000)
add_test(NAME GOLDEN_decode_45E0 COMMAND test_golden_decode_45E0)
# FLASHDUMP-1 (2026-10-08): the MS2000 IPL (firmware update) protocol + the flash-dump patch, host side, and its
# end-to-end test on the emulated machine (install the patched update, recover from an error, boot, dump 1 MB)
add_library(ms2k_ipl STATIC source/tools/flashdump/ms2k_ipl.cpp)
target_include_directories(ms2k_ipl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source)
add_executable(ipl_e2e EXCLUDE_FROM_ALL source/tests/ipl_e2e.cpp)
target_include_directories(ipl_e2e PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(ipl_e2e ms2000 ms2k_ipl)
# AUDIT-1 (2026-10-09): the firmware's factory test modes on the emulated machine (LCD / LED / MIDI transcript)
add_executable(testmode_probe EXCLUDE_FROM_ALL source/tests/testmode_probe.cpp)
target_include_directories(testmode_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(testmode_probe ms2000)
# AUDIO-IN round (2026-09-26): the AK4522 ADC model against its datasheet numbers
add_executable(test_ak4522 source/tests/test_ak4522.cpp)
target_include_directories(test_ak4522 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
add_test(NAME AK4522_adc_model COMMAND test_ak4522)
set_tests_properties(GOLDEN_decode_45E0 PROPERTIES LABELS "golden;gate;R5")

add_executable(test_subx_cycles source/tests/test_subx_cycles.cpp)
target_link_libraries(test_subx_cycles ms2000)
add_test(NAME SUBX_cycles COMMAND test_subx_cycles)
set_tests_properties(SUBX_cycles PROPERTIES LABELS "arith;P1.6;cycles")

# --- SHAR tests ---
add_executable(test_ssot_shar_decode source/tests/test_ssot_shar_decode.cpp)
add_executable(test_shar source/tests/test_shar.cpp)
target_link_libraries(test_ssot_shar_decode ms2000)
target_link_libraries(test_shar ms2000)

add_test(NAME SHAR_ssot_smoke COMMAND test_ssot_shar_decode)
add_test(NAME SHAR_matrix COMMAND test_shar)
set_tests_properties(SHAR_ssot_smoke SHAR_matrix PROPERTIES LABELS "arith;P1.7;ssot")

# SHAR cycle-smoke
add_executable(test_shar_cycles source/tests/test_shar_cycles.cpp)
target_link_libraries(test_shar_cycles ms2000)
add_test(NAME SHAR_cycles COMMAND test_shar_cycles)
set_tests_properties(SHAR_cycles PROPERTIES LABELS "arith;P1.7;cycles")

# Ensure guards propagate to new targets too
list(APPEND H8S_ALL_TARGETS test_ssot_shar_decode test_shar test_shar_cycles)

# --- P1.8: SHIFT tests (SHLL/SHLR/ROTL/ROTR) ---
add_executable(test_ssot_shift_decode source/tests/test_ssot_shift_decode.cpp)
target_link_libraries(test_ssot_shift_decode ms2000)
add_test(NAME SHIFT_ssot_smoke COMMAND test_ssot_shift_decode)
set_tests_properties(SHIFT_ssot_smoke PROPERTIES LABELS "arith;P1.8;ssot")

add_executable(test_shift_matrix source/tests/test_shift_matrix.cpp)
target_link_libraries(test_shift_matrix ms2000)
add_test(NAME SHIFT_matrix COMMAND test_shift_matrix)
set_tests_properties(SHIFT_matrix PROPERTIES LABELS "arith;P1.8;matrix")

add_executable(test_shift_cycles source/tests/test_shift_cycles.cpp)
target_link_libraries(test_shift_cycles ms2000)
add_test(NAME SHIFT_cycles COMMAND test_shift_cycles)
set_tests_properties(SHIFT_cycles PROPERTIES LABELS "arith;P1.8;cycles")

list(APPEND H8S_ALL_TARGETS test_ssot_shift_decode test_shift_matrix test_shift_cycles)

# --- P1.9: ROTX (through-carry) + SHAL tests ---
add_executable(test_ssot_rotc_decode source/tests/test_ssot_rotc_decode.cpp)
target_link_libraries(test_ssot_rotc_decode ms2000)
add_test(NAME ROTC_ssot_smoke COMMAND test_ssot_rotc_decode)
set_tests_properties(ROTC_ssot_smoke PROPERTIES LABELS "arith;P1.9;ssot")

add_executable(test_rotc_matrix source/tests/test_rotc_matrix.cpp)
target_link_libraries(test_rotc_matrix ms2000)
add_test(NAME ROTC_matrix COMMAND test_rotc_matrix)
set_tests_properties(ROTC_matrix PROPERTIES LABELS "arith;P1.9;matrix")

add_executable(test_rotc_cycles source/tests/test_rotc_cycles.cpp)
target_link_libraries(test_rotc_cycles ms2000)
add_test(NAME ROTC_cycles COMMAND test_rotc_cycles)
set_tests_properties(ROTC_cycles PROPERTIES LABELS "arith;P1.9;cycles")

list(APPEND H8S_ALL_TARGETS test_ssot_rotc_decode test_rotc_matrix test_rotc_cycles)

# --- P1.10: ADDX (with extend) tests ---
add_executable(test_ssot_addx_decode source/tests/test_ssot_addx_decode.cpp)
target_link_libraries(test_ssot_addx_decode ms2000)
add_test(NAME ADDX_ssot_smoke COMMAND test_ssot_addx_decode)
set_tests_properties(ADDX_ssot_smoke PROPERTIES LABELS "ssot;arith;P1.10")

add_executable(test_addx_matrix source/tests/test_addx_matrix.cpp)
target_link_libraries(test_addx_matrix ms2000)
add_test(NAME ADDX_matrix COMMAND test_addx_matrix)
set_tests_properties(ADDX_matrix PROPERTIES LABELS "arith;P1.10;matrix")

add_executable(test_addx_cycles source/tests/test_addx_cycles.cpp)
target_link_libraries(test_addx_cycles ms2000)
add_test(NAME ADDX_cycles COMMAND test_addx_cycles)
set_tests_properties(ADDX_cycles PROPERTIES LABELS "arith;P1.10;cycles")

add_executable(test_addx_chain source/tests/test_addx_chain.cpp)
target_link_libraries(test_addx_chain ms2000)
add_test(NAME ADDX_chain COMMAND test_addx_chain)
# BUG112: 16-bit register fields (E0-E7) and CMP.B/W Rs,Rd, HM Appendix A rendered p.807
add_executable(test_regfield_word source/tests/test_regfield_word.cpp)
target_link_libraries(test_regfield_word ms2000)
add_test(NAME REGFIELD_word COMMAND test_regfield_word)
# BUG113: every shift/rotate encoding (10/11/12/13), HM Appendix A.2 rendered p.804-805
add_executable(test_shift_all source/tests/test_shift_all.cpp)
target_link_libraries(test_shift_all ms2000)
add_test(NAME SHIFT_all COMMAND test_shift_all)
add_executable(test_bitlogic_reg source/tests/test_bitlogic_reg.cpp)
target_link_libraries(test_bitlogic_reg ms2000)
add_test(NAME BITLOGIC_reg COMMAND test_bitlogic_reg)
add_executable(test_bitmem_abs source/tests/test_bitmem_abs.cpp)
target_link_libraries(test_bitmem_abs ms2000)
add_test(NAME BITMEM_abs COMMAND test_bitmem_abs)
# BUG121: flag/length defects found by tools/diffref, exams from the rendered manual pages
add_executable(test_bug121_flags source/tests/test_bug121_flags.cpp)
target_link_libraries(test_bug121_flags ms2000)
add_test(NAME BUG121_flags COMMAND test_bug121_flags)
# BUG122: MOV.W @ERs+/@-ERd 24-bit address, MOV Rs,@(d:32) flags - found by the diffref MOV oracle
add_executable(test_bug122_mov source/tests/test_bug122_mov.cpp)
target_link_libraries(test_bug122_mov ms2000)
add_test(NAME BUG122_mov COMMAND test_bug122_mov)
set_tests_properties(ADDX_chain PROPERTIES LABELS "arith;P1.10;property")

list(APPEND H8S_ALL_TARGETS test_ssot_addx_decode test_addx_matrix test_addx_cycles test_addx_chain)

# --- Property fuzzers ---
add_executable(test_addx_subx_chain_fuzz source/tests/test_addx_subx_chain_fuzz.cpp)
target_link_libraries(test_addx_subx_chain_fuzz ms2000)
add_test(NAME ADDX_SUBX_chain_fuzz COMMAND test_addx_subx_chain_fuzz)
set_tests_properties(ADDX_SUBX_chain_fuzz PROPERTIES LABELS "arith;property;P1.10;P1.6")

add_executable(test_rotx_property_fuzz source/tests/test_rotx_property_fuzz.cpp)
target_link_libraries(test_rotx_property_fuzz ms2000)
add_test(NAME ROTX_property_fuzz COMMAND test_rotx_property_fuzz)
set_tests_properties(ROTX_property_fuzz PROPERTIES LABELS "arith;property;P1.9")

list(APPEND H8S_ALL_TARGETS test_addx_subx_chain_fuzz test_rotx_property_fuzz)

# --- P1.11 decode smoke ---
add_executable(test_ssot_p111_decode source/tests/test_ssot_p111_decode.cpp)
target_link_libraries(test_ssot_p111_decode ms2000)
add_test(NAME P1_11_ssot_smoke COMMAND test_ssot_p111_decode)
set_tests_properties(P1_11_ssot_smoke PROPERTIES LABELS "ssot;P1.11")
list(APPEND H8S_ALL_TARGETS test_ssot_p111_decode)

# --- P1.11: matrices ---
add_executable(test_daa_das_matrix source/tests/test_daa_das_matrix.cpp)
target_link_libraries(test_daa_das_matrix ms2000)
add_test(NAME P1_11_daa_das_matrix COMMAND test_daa_das_matrix)
set_tests_properties(P1_11_daa_das_matrix PROPERTIES LABELS "arith;P1.11;matrix")

add_executable(test_ext_matrix source/tests/test_ext_matrix.cpp)
target_link_libraries(test_ext_matrix ms2000)
add_test(NAME P1_11_ext_matrix COMMAND test_ext_matrix)
set_tests_properties(P1_11_ext_matrix PROPERTIES LABELS "arith;P1.11;matrix")

add_executable(test_neg_matrix source/tests/test_neg_matrix.cpp)
target_link_libraries(test_neg_matrix ms2000)
add_test(NAME P1_11_neg_matrix COMMAND test_neg_matrix)
set_tests_properties(P1_11_neg_matrix PROPERTIES LABELS "arith;P1.11;matrix")

add_executable(test_mulx_matrix source/tests/test_mulx_matrix.cpp)
target_link_libraries(test_mulx_matrix ms2000)
add_test(NAME P1_11_mulx_matrix COMMAND test_mulx_matrix)
set_tests_properties(P1_11_mulx_matrix PROPERTIES LABELS "arith;P1.11;matrix")

list(APPEND H8S_ALL_TARGETS test_daa_das_matrix test_ext_matrix test_neg_matrix test_mulx_matrix)

# ---- SHAR/SUBX clash guards -----------------------------------------------
# When ON, we add -DH8S_HAS_SHAR_SSOT=1 to all H8S targets.
# Leave OFF until SHAR SSoT actually lands (to avoid undefined H8S::SHAR symbols).
option(H8S_ENABLE_SHAR_SSOT "Enable SHAR SSoT clash guards (requires SHAR SSoT present)" OFF)

# Helper function to propagate compile defs to a list of targets
function(h8s_propagate_flag_to_targets flag)
  foreach(tgt IN LISTS ARGN)
    if(TARGET ${tgt})
      target_compile_definitions(${tgt} PRIVATE
        $<$<BOOL:${${flag}}>:H8S_HAS_SHAR_SSOT=1>)
    endif()
  endforeach()
endfunction()

# Core + tests you want the guard on (adjust names if needed)
set(H8S_ALL_TARGETS
  ms2000            # core lib
  ms2000_emulator   # main executable
  memory_penalty_test
  test_0x7A_mem_penalty
  test_subx
  test_ssot_subx_decode
  test_subx_cycles
)

# Optional auto-detect: turn the guard ON automatically if SHAR SSoT header exists.
# Leave this OFF if you want fully manual control.
option(H8S_SHAR_SSOT_AUTODETECT "Auto-enable SHAR SSoT clash guards when SHAR SSoT header is present" OFF)

if(H8S_SHAR_SSOT_AUTODETECT AND NOT H8S_ENABLE_SHAR_SSOT)
  set(_H8S_SHAR_SSOT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/source/core/h8s2350_shar_contracts.h")
  if(EXISTS "${_H8S_SHAR_SSOT_HEADER}")
    message(STATUS "Auto-enabling H8S_ENABLE_SHAR_SSOT (detected: ${_H8S_SHAR_SSOT_HEADER}).")
    set(H8S_ENABLE_SHAR_SSOT ON CACHE BOOL "Enable SHAR SSoT clash guards" FORCE)
  endif()
endif()

h8s_propagate_flag_to_targets(H8S_ENABLE_SHAR_SSOT ${H8S_ALL_TARGETS})

message(STATUS "H8S_ENABLE_SHAR_SSOT = ${H8S_ENABLE_SHAR_SSOT}")

# --- P1.5 Extended Branch Tests QUARANTINED (fictional Bcc16 family) ---
# Moved to source/tests/quarantine/ per R5. Real 16-bit Bcc = 0x58 [cc]0 disp16.
# 0x64-0x6B = OR.W/XOR.W/AND.W/MOV @ERs,Rd/MOV abs (per manual)
# 0x7C-0x7F = bit-manipulation groups (per manual)

# Build information
message(STATUS "Korg MS-2000 Emulator Build Configuration:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Emulate MCU: ${MS2K_EMULATE_MCU}")
message(STATUS "  Host UI: ${MS2K_HOST_UI}")
message(STATUS "  Debug: ${MS2K_DEBUG}")
message(STATUS "  Essential targets only: 1 executable (ms2000_emulator.exe only as requested)")
# --- P1.11: cycles ---
add_executable(test_mulx_cycles source/tests/test_mulx_cycles.cpp)
target_link_libraries(test_mulx_cycles ms2000)
add_test(NAME MULX_cycles COMMAND test_mulx_cycles)
set_tests_properties(MULX_cycles PROPERTIES LABELS "arith;P1.11;cycles")
list(APPEND H8S_ALL_TARGETS test_mulx_cycles)
# --- P1.11: negative decode guards ---
add_executable(test_ssot_mulx_decode_neg source/tests/test_ssot_mulx_decode_neg.cpp)
target_link_libraries(test_ssot_mulx_decode_neg ms2000)
add_test(NAME P1_11_mulx_decode_neg COMMAND test_ssot_mulx_decode_neg)
set_tests_properties(P1_11_mulx_decode_neg PROPERTIES LABELS "ssot;P1.11;decode")
list(APPEND H8S_ALL_TARGETS test_ssot_mulx_decode_neg)
# --- P1.11: flags sentinels ---
add_executable(test_mulx_flags source/tests/test_mulx_flags.cpp)
target_link_libraries(test_mulx_flags ms2000)
add_test(NAME MULX_flags_preserve COMMAND test_mulx_flags)
set_tests_properties(MULX_flags_preserve PROPERTIES LABELS "arith;P1.11;flags")
list(APPEND H8S_ALL_TARGETS test_mulx_flags)


# In Debug test binaries, wall off legacy ALU tables/heuristics so SSoT intercepts stay authoritative
get_property(_ALL_TGTS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t ${_ALL_TGTS})
  if(_t MATCHES "^test_")
    target_compile_definitions(${_t} PRIVATE
      $<$<CONFIG:Debug>:H8S_DISABLE_LEGACY_ALU_TABLE=1>
      $<$<CONFIG:Debug>:H8S_DISABLE_LEGACY_ALU_HEURISTICS=1>)
  endif()
endforeach()

# FIX19: MSVC default stack (1MB) is too small for H8S2350Emulator instantiation on stack (~1.2MB+)
# Apply /STACK:8388608 (8MB) linker flag to all test targets
get_property(_ALL_TGTS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t ${_ALL_TGTS})
  if(_t MATCHES "^test_")
    if(MSVC)
      target_link_options(${_t} PRIVATE /STACK:8388608)
    endif()
  endif()
endforeach()

# BUG104 measurement tool: runs the local vintage DSP56362 boot ROM in the upstream dsp56kEmu.
add_executable(dsp_bootrom_probe tools/dsp_bootrom_probe.cpp)
target_include_directories(dsp_bootrom_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/dsp56300/source/dsp56kEmu ${CMAKE_CURRENT_SOURCE_DIR}/external/dsp56300/source)
target_link_libraries(dsp_bootrom_probe PRIVATE dsp56kEmu)

# DIFFREF (2026-09-25): LOCAL-ONLY instruction-level differential test against the
# UKNTCH2000 reference H8S core. The reference source is never copied into this repo;
# configure with -DMS2K_REFCORE=<path to a UKNTCH2000 checkout> to build it.
set(MS2K_REFCORE "" CACHE PATH "UKNTCH2000 checkout for the local-only differential test (tools/diffref)")
if(MS2K_REFCORE AND EXISTS "${MS2K_REFCORE}/src/h8s.c")
  enable_language(C)
  add_executable(diffref tools/diffref/diffref.cpp tools/diffref/ref_wrap.c)
  target_include_directories(diffref BEFORE PRIVATE "${MS2K_REFCORE}/include")   # dsp56kEmu has its own types.h
  set_source_files_properties(tools/diffref/ref_wrap.c PROPERTIES COMPILE_DEFINITIONS "MS2K_REF_H8S=\"${MS2K_REFCORE}/src/h8s.c\"")
  target_link_libraries(diffref ms2000)
  if(MSVC)
    target_link_options(diffref PRIVATE /STACK:8388608)
  endif()
endif()

# VST3 phase 1 (2026-09-28): the MS2000R plugin around Ms2kRunner::render(), with JUCE fetched at configure
# time. Off by default; built in its own folder: cmake -S . -B build_vst -DMS2K_BUILD_VST3=ON
option(MS2K_BUILD_VST3 "Build the MS2000R VST3 plugin (JUCE)" OFF)
if(MS2K_BUILD_VST3)
    include(FetchContent)
    # the JUCE fork gearmulator builds with (JUCE 7.0.10)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/dsp56300/JUCE.git
                              GIT_TAG bf39bb3916cf6e5858a53831440eb1e58e5fb095)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(source/plugin)
endif()
