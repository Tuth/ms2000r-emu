# MS2000R VST3 (phase 1; phase 2 = VST3-2: the vector panel editor, state in the project). Built only with -DMS2K_BUILD_VST3=ON, in its own build folder (build_vst).
juce_add_plugin(MS2000R_VST
    COMPANY_NAME "MS2000 emulation project"
    PRODUCT_NAME "MS2000R"
    PLUGIN_MANUFACTURER_CODE Msem
    PLUGIN_CODE Ms2r
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

juce_generate_juce_header(MS2000R_VST)

target_sources(MS2000R_VST PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_include_directories(MS2000R_VST PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/source)
target_compile_definitions(MS2000R_VST PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(MS2000R_VST PRIVATE ms2000
    juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# A console host that loads the built .vst3 like a DAW does and renders a note through processBlock.
juce_add_console_app(MS2000R_HostTest PRODUCT_NAME "MS2000R_HostTest")
juce_generate_juce_header(MS2000R_HostTest)
target_sources(MS2000R_HostTest PRIVATE HostTest.cpp)
target_compile_definitions(MS2000R_HostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MS2000R_HostTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)

# MULTI-1 (2026-10-02): several instances per process. The host loads this small VST3 module; the JUCE plugin above
# travels in the bundle as Contents/Resources/MS2000R_engine.dll and every instance after the first runs from its
# own copy of it (see shim/ms2k_vst3_shim.cpp). Result: build_vst/VST3_multi/MS2000R.vst3
if(WIN32)
    set(MS2K_VST3SDK ${JUCE_MODULES_DIR}/juce_audio_processors/format_types/VST3_SDK)   # the VST3 SDK headers JUCE carries
    add_library(MS2000R_Shim SHARED shim/ms2k_vst3_shim.cpp
        ${MS2K_VST3SDK}/pluginterfaces/base/coreiids.cpp ${MS2K_VST3SDK}/pluginterfaces/base/funknown.cpp)
    target_include_directories(MS2000R_Shim PRIVATE ${MS2K_VST3SDK})
    target_link_libraries(MS2000R_Shim PRIVATE ole32)
    set_target_properties(MS2000R_Shim PROPERTIES OUTPUT_NAME MS2000R SUFFIX .vst3)
    add_dependencies(MS2000R_Shim MS2000R_VST_VST3)
    set(MS2K_MULTI ${CMAKE_BINARY_DIR}/VST3_multi/MS2000R.vst3)
    add_custom_command(TARGET MS2000R_Shim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${MS2K_MULTI}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MS2K_MULTI}/Contents/x86_64-win ${MS2K_MULTI}/Contents/Resources
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:MS2000R_VST_VST3> ${MS2K_MULTI}/Contents/Resources/MS2000R_engine.dll
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:MS2000R_Shim> ${MS2K_MULTI}/Contents/x86_64-win/MS2000R.vst3)
endif()
