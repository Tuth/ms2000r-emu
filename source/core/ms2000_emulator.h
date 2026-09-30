#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>

// DSP56000 emulátor include-ok
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/types.h"

namespace MS2000
{
    // Korg MS-2000 specifikus konstansok
    static constexpr uint32_t MS2000_PROGRAM_MEMORY_SIZE = 0x10000;  // 64KB
    static constexpr uint32_t MS2000_DATA_MEMORY_SIZE = 0x10000;     // 64KB
    static constexpr uint32_t MS2000_NVRAM_SIZE = 0x2000;           // 8KB
    
    // Memória címek
    static constexpr uint32_t MS2000_FIRMWARE_START = 0x0000;
    static constexpr uint32_t MS2000_FIRMWARE_END = 0x3FFF;
    static constexpr uint32_t MS2000_TABLES_START = 0x4000;
    static constexpr uint32_t MS2000_TABLES_END = 0x7FFF;
    
    // NVRAM címek
    static constexpr uint32_t MS2000_SYSTEM_SETTINGS_START = 0x0000;
    static constexpr uint32_t MS2000_SYSTEM_SETTINGS_END = 0x00FF;
    static constexpr uint32_t MS2000_PRESETS_START = 0x0100;
    static constexpr uint32_t MS2000_PRESETS_END = 0x0FFF;
    
    // Preset struktúra (256 byte per preset)
    struct MS2000Preset
    {
        std::array<uint8_t, 256> data;
        std::string name;
        bool is_valid;
    };
    
    // Rendszer beállítások
    struct MS2000SystemSettings
    {
        uint8_t master_volume;
        uint8_t midi_channel;
        uint8_t transpose;
        uint8_t tuning;
        uint8_t arpeggiator_enabled;
        uint8_t arpeggiator_pattern;
        uint8_t arpeggiator_octaves;
        uint8_t delay_enabled;
        uint8_t delay_time;
        uint8_t delay_feedback;
        uint8_t reverb_enabled;
        uint8_t reverb_level;
        uint8_t reserved[244]; // 256 - 12 = 244 byte
    };
    
    // Korg MS-2000 specifikus perifériák
    class MS2000Peripherals : public dsp56k::IPeripherals
    {
    public:
        MS2000Peripherals();
        ~MS2000Peripherals() override = default;
        
        // Audio I/O
        void processAudio(float* left_out, float* right_out, int num_samples);
        void setAudioSampleRate(float sample_rate);
        
        // MIDI I/O
        void processMIDI(const uint8_t* midi_data, int midi_length);
        void sendMIDI(const uint8_t* midi_data, int midi_length);
        
        // Panel vezérlők
        void setOscillatorWaveform(int osc, int waveform);
        void setOscillatorPitch(int osc, float pitch);
        void setOscillatorDetune(int osc, float detune);
        void setFilterCutoff(float cutoff);
        void setFilterResonance(float resonance);
        void setFilterType(int type);
        void setEnvelopeAttack(float attack);
        void setEnvelopeDecay(float decay);
        void setEnvelopeSustain(float sustain);
        void setEnvelopeRelease(float release);
        void setLFORate(float rate);
        void setLFODepth(float depth);
        void setLFOShape(int shape);
        
        // Preset kezelés
        bool loadPreset(int preset_number);
        bool savePreset(int preset_number, const std::string& name);
        std::vector<MS2000Preset> getPresets() const;
        
        // NVRAM kezelés
        bool loadNVRAM(const std::string& filename);
        bool saveNVRAM(const std::string& filename);
        
    private:
        // Audio buffer-ek
        std::vector<float> m_audio_buffer_left;
        std::vector<float> m_audio_buffer_right;
        float m_sample_rate;
        
        // MIDI buffer-ek
        std::vector<uint8_t> m_midi_in_buffer;
        std::vector<uint8_t> m_midi_out_buffer;
        
        // Panel állapot
        struct PanelState
        {
            // Oszcillátorok
            std::array<int, 2> oscillator_waveform;
            std::array<float, 2> oscillator_pitch;
            std::array<float, 2> oscillator_detune;
            
            // Szűrő
            float filter_cutoff;
            float filter_resonance;
            int filter_type;
            
            // Envelope
            float envelope_attack;
            float envelope_decay;
            float envelope_sustain;
            float envelope_release;
            
            // LFO
            float lfo_rate;
            float lfo_depth;
            int lfo_shape;
        } m_panel_state;
        
        // Preset-ek
        std::array<MS2000Preset, 64> m_presets;
        MS2000SystemSettings m_system_settings;
        
        // NVRAM fájl
        std::string m_nvram_filename;
    };
    
    // Fő emulátor osztály
    class MS2000Emulator
    {
    public:
        MS2000Emulator();
        ~MS2000Emulator();
        
        // Inicializálás
        bool initialize();
        void shutdown();
        
        // Firmware kezelés
        bool loadFirmware(const std::string& firmware_path);
        bool isFirmwareLoaded() const { return m_firmware_loaded; }
        
        // Audio feldolgozás
        void processAudio(float* left_out, float* right_out, int num_samples);
        void setAudioSampleRate(float sample_rate);
        
        // MIDI feldolgozás
        void processMIDI(const uint8_t* midi_data, int midi_length);
        void sendMIDI(const uint8_t* midi_data, int midi_length);
        
        // Panel vezérlők
        void setOscillatorWaveform(int osc, int waveform);
        void setOscillatorPitch(int osc, float pitch);
        void setOscillatorDetune(int osc, float detune);
        void setFilterCutoff(float cutoff);
        void setFilterResonance(float resonance);
        void setFilterType(int type);
        void setEnvelopeAttack(float attack);
        void setEnvelopeDecay(float decay);
        void setEnvelopeSustain(float sustain);
        void setEnvelopeRelease(float release);
        void setLFORate(float rate);
        void setLFODepth(float depth);
        void setLFOShape(int shape);
        
        // Preset kezelés
        bool loadPreset(int preset_number);
        bool savePreset(int preset_number, const std::string& name);
        std::vector<MS2000Preset> getPresets() const;
        
        // NVRAM kezelés
        bool loadNVRAM(const std::string& filename);
        bool saveNVRAM(const std::string& filename);
        
        // Debug és diagnosztika
        void reset();
        void step();
        void run();
        void pause();
        bool isRunning() const { return m_running; }
        
        // Memória hozzáférés
        uint32_t readMemory(uint32_t address, uint32_t size = 1);
        void writeMemory(uint32_t address, uint32_t value, uint32_t size = 1);
        
        // Regiszter hozzáférés
        uint32_t readRegister(uint32_t reg);
        void writeRegister(uint32_t reg, uint32_t value);
        
    private:
        // DSP56000 emulátor komponensek
        std::unique_ptr<dsp56k::Memory> m_memory;
        std::unique_ptr<MS2000Peripherals> m_peripherals;
        std::unique_ptr<dsp56k::DSP> m_dsp;
        
        // Állapot
        bool m_initialized;
        bool m_firmware_loaded;
        bool m_running;
        float m_sample_rate;
        
        // Firmware adatok
        std::vector<uint8_t> m_firmware_data;
        
        // NVRAM adatok
        std::vector<uint8_t> m_nvram_data;
        std::string m_nvram_filename;
        
        // Debug és diagnosztika
        void initializeMemory();
        void initializePeripherals();
        void loadDefaultPresets();
        void loadDefaultSystemSettings();
    };
}
