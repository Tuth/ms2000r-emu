#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace MS2000 {

// ===== MS2000 Hardware Mapping - Based on Real Service Data =====

// State enums for multifunction controls
enum class TimbreSelect : uint8_t { TIMBRE_1 = 0, TIMBRE_2 = 1 };
enum class FXMode : uint8_t { MOD = 0, DELAY = 1 };
enum class LFOSelect : uint8_t { LFO_1 = 1, LFO_2 = 2 };
enum class SeqMode : uint8_t { SEQ_OFF = 0, SEQ_ON = 1 };
enum class SeqLane : uint8_t { LANE_LEVEL = 0, LANE_PAN = 1, LANE_PITCH = 2 };

// Hardware state structure
struct HardwareState {
    TimbreSelect timbre = TimbreSelect::TIMBRE_1;
    FXMode fx = FXMode::MOD;
    LFOSelect lfo = LFOSelect::LFO_1;
    SeqMode seq = SeqMode::SEQ_OFF;
    SeqLane lane = SeqLane::LANE_LEVEL;
};

// ===== A/D Converter Mapping (4 Banks, 8 MUX channels each) =====

// ADC Bank definitions
enum class ADCBank : uint8_t {
    BANK_NONE = 0,  // Analog-only controls
    BANK_4 = 4,     // EG Section
    BANK_5 = 5,     // LFO/FX/VP Section  
    BANK_6 = 6,     // OSC/Mixer/Portamento
    BANK_7 = 7,     // Filter/Amp/Arp
    BANK_8 = 8,     // Multifunction Knobs 1-8
    BANK_9 = 9      // Multifunction Knobs 9-16
};

// ADC MUX channel definitions
enum class ADCMux : uint8_t {
    MUX_NONE = 0xFF,  // For analog-only controls
    MUX_0 = 0, MUX_1 = 1, MUX_2 = 2, MUX_3 = 3,
    MUX_4 = 4, MUX_5 = 5, MUX_6 = 6, MUX_7 = 7
};

// Knob parameter mapping
struct KnobMapping {
    std::string name;
    ADCBank bank;
    ADCMux mux;
    std::string baseRole;
    std::vector<std::pair<std::string, std::string>> multiRoles; // condition -> role
    
    KnobMapping(const std::string& n, ADCBank b, ADCMux m, const std::string& br)
        : name(n), bank(b), mux(m), baseRole(br) {}
    
    KnobMapping(const std::string& n, ADCBank b, ADCMux m, const std::string& br, 
                const std::vector<std::pair<std::string, std::string>>& roles)
        : name(n), bank(b), mux(m), baseRole(br), multiRoles(roles) {}
};

        // Real MS2000 knob mappings from service data (35 knobs total)
        static const std::vector<KnobMapping> MS2000_KNOB_MAPPINGS = {
    // Analog-only controls (no ADC bank/mux) - 3 knobs
    {"POWER VOLUME", ADCBank::BANK_NONE, ADCMux::MUX_NONE, "MasterVolume"},
    {"AUDIO IN 1 LEVEL", ADCBank::BANK_NONE, ADCMux::MUX_NONE, "AudioIn1Level"},
    {"AUDIO IN 2 LEVEL", ADCBank::BANK_NONE, ADCMux::MUX_NONE, "AudioIn2Level"},
    
    // Bank 4 - Dedicated EG Section - 8 knobs
    {"EG1 ATTACK", ADCBank::BANK_4, ADCMux::MUX_0, "EG1.Attack"},
    {"EG1 DECAY", ADCBank::BANK_4, ADCMux::MUX_1, "EG1.Decay"},
    {"EG1 SUSTAIN", ADCBank::BANK_4, ADCMux::MUX_2, "EG1.Sustain"},
    {"EG1 RELEASE", ADCBank::BANK_4, ADCMux::MUX_3, "EG1.Release"},
    {"EG2 ATTACK", ADCBank::BANK_4, ADCMux::MUX_4, "EG2.Attack"},
    {"EG2 DECAY", ADCBank::BANK_4, ADCMux::MUX_5, "EG2.Decay"},
    {"EG2 SUSTAIN", ADCBank::BANK_4, ADCMux::MUX_6, "EG2.Sustain"},
    {"EG2 RELEASE", ADCBank::BANK_4, ADCMux::MUX_7, "EG2.Release"},
    
    // Bank 5 - Dedicated LFO/FX/VP Section - 7 knobs
    {"LFO FREQUENCY", ADCBank::BANK_5, ADCMux::MUX_0, "LFO1.Frequency", 
        {{"LFO=2", "LFO2.Frequency"}}},
    {"EFFECT SPEED/TIME", ADCBank::BANK_5, ADCMux::MUX_2, "Mod.Speed",
        {{"FX=DELAY", "Delay.Time"}}},
    {"EFFECT DEPTH/FEEDBACK", ADCBank::BANK_5, ADCMux::MUX_3, "Mod.Depth",
        {{"FX=DELAY", "Delay.Feedback"}}},
    {"VP PATCH1 DEPTH", ADCBank::BANK_5, ADCMux::MUX_4, "VP.Patch1.Depth"},
    {"VP PATCH2 DEPTH", ADCBank::BANK_5, ADCMux::MUX_5, "VP.Patch2.Depth"},
    {"VP PATCH3 DEPTH", ADCBank::BANK_5, ADCMux::MUX_6, "VP.Patch3.Depth"},
    {"VP PATCH4 DEPTH", ADCBank::BANK_5, ADCMux::MUX_7, "VP.Patch4.Depth"},
    
    // Bank 6 - Dedicated OSC/Mixer/Portamento - 7 knobs
    {"OSC1 CONTROL 1", ADCBank::BANK_6, ADCMux::MUX_0, "OSC1.Control1"},
    {"OSC1 CONTROL 2 / SEMITONE", ADCBank::BANK_6, ADCMux::MUX_1, "OSC1.Control2"},
    {"OSC2 TUNE", ADCBank::BANK_6, ADCMux::MUX_2, "OSC2.Tune"},
    {"MIXER NOISE LEVEL", ADCBank::BANK_6, ADCMux::MUX_3, "Mixer.Noise"},
    {"MIXER OSC2 LEVEL", ADCBank::BANK_6, ADCMux::MUX_4, "Mixer.Osc2"},
    {"MIXER OSC1 LEVEL", ADCBank::BANK_6, ADCMux::MUX_5, "Mixer.Osc1"},
    {"PORTAMENTO TIME", ADCBank::BANK_6, ADCMux::MUX_6, "Portamento.Time"},
    
    // Bank 7 - Dedicated Filter/Amp/Arp - 9 knobs
    {"FILTER CUTOFF", ADCBank::BANK_7, ADCMux::MUX_0, "Filter.Cutoff"},
    {"FILTER EG1 INT", ADCBank::BANK_7, ADCMux::MUX_1, "Filter.EG1Int"},
    {"FILTER RESONANCE", ADCBank::BANK_7, ADCMux::MUX_2, "Filter.Resonance"},
    {"FILTER KBD TRACK", ADCBank::BANK_7, ADCMux::MUX_3, "Filter.KBDTrack"},
    {"AMP LEVEL", ADCBank::BANK_7, ADCMux::MUX_4, "Amp.Level (timbre1)",
        {{"TIMBRE=2", "Amp.Level (timbre2)"}}},
    {"ARPEGGIATOR TEMPO", ADCBank::BANK_7, ADCMux::MUX_5, "Arp.Tempo"},
    {"ARPEGGIATOR GATE", ADCBank::BANK_7, ADCMux::MUX_6, "Arp.Gate"},
    {"AMP PAN", ADCBank::BANK_7, ADCMux::MUX_7, "Amp.Pan (timbre1)",
        {{"TIMBRE=2", "Amp.Pan (timbre2)"}}},
    
    // Bank 8 - Multifunctional Step Sequencer/Synth Control Knobs (16 knobs)
    // Based on user's exact mapping specification
    {"STEP SEQ KNOB 1", ADCBank::BANK_8, ADCMux::MUX_0, "EG1.Attack",
        {{"SEQ=ON", "Seq1.Level[1]"}}},
    {"STEP SEQ KNOB 2", ADCBank::BANK_8, ADCMux::MUX_1, "EG1.Decay",
        {{"SEQ=ON", "Seq1.Level[2]"}}},
    {"STEP SEQ KNOB 3", ADCBank::BANK_8, ADCMux::MUX_2, "EG1.Sustain",
        {{"SEQ=ON", "Seq1.Level[3]"}}},
    {"STEP SEQ KNOB 4", ADCBank::BANK_8, ADCMux::MUX_3, "EG1.Release",
        {{"SEQ=ON", "Seq1.Level[4]"}}},
    {"STEP SEQ KNOB 5", ADCBank::BANK_8, ADCMux::MUX_4, "EG2.Attack",
        {{"SEQ=ON", "Seq1.Level[5]"}}},
    {"STEP SEQ KNOB 6", ADCBank::BANK_8, ADCMux::MUX_5, "EG2.Decay",
        {{"SEQ=ON", "Seq1.Level[6]"}}},
    {"STEP SEQ KNOB 7", ADCBank::BANK_8, ADCMux::MUX_6, "EG2.Sustain",
        {{"SEQ=ON", "Seq1.Level[7]"}}},
    {"STEP SEQ KNOB 8", ADCBank::BANK_8, ADCMux::MUX_7, "EG2.Release",
        {{"SEQ=ON", "Seq1.Level[8]"}}},
    {"STEP SEQ KNOB 9", ADCBank::BANK_9, ADCMux::MUX_0, "LFO1.Frequency",
        {{"SEQ=ON", "Seq1.Level[9]"}}},
    {"STEP SEQ KNOB 10", ADCBank::BANK_9, ADCMux::MUX_1, "LFO1.Speed",
        {{"SEQ=ON", "Seq1.Level[10]"}}},
    {"STEP SEQ KNOB 11", ADCBank::BANK_9, ADCMux::MUX_2, "LFO2.Frequency",
        {{"SEQ=ON", "Seq1.Level[11]"}}},
    {"STEP SEQ KNOB 12", ADCBank::BANK_9, ADCMux::MUX_3, "LFO2.Speed",
        {{"SEQ=ON", "Seq1.Level[12]"}}},
    {"STEP SEQ KNOB 13", ADCBank::BANK_9, ADCMux::MUX_4, "VP.Patch1.Depth",
        {{"SEQ=ON", "Seq1.Level[13]"}}},
    {"STEP SEQ KNOB 14", ADCBank::BANK_9, ADCMux::MUX_5, "VP.Patch2.Depth",
        {{"SEQ=ON", "Seq1.Level[14]"}}},
    {"STEP SEQ KNOB 15", ADCBank::BANK_9, ADCMux::MUX_6, "VP.Patch3.Depth",
        {{"SEQ=ON", "Seq1.Level[15]"}}},
    {"STEP SEQ KNOB 16", ADCBank::BANK_9, ADCMux::MUX_7, "VP.Patch4.Depth",
        {{"SEQ=ON", "Seq1.Level[16]"}}}
};

// ===== Button Matrix Mapping (8x8) =====

// Button types
enum class ButtonType : uint8_t {
    MOMENTARY = 0,  // Press and release
    TOGGLE = 1,     // Press to toggle state
    LATCH2 = 2,     // Latch between 2 states
    LATCH3 = 3      // Latch between 3 states
};

// Button mapping structure
struct ButtonMapping {
    std::string name;
    std::string key;
    ButtonType type;
    std::string group;
    std::vector<std::string> options;
    uint8_t row;
    uint8_t col;
    
    ButtonMapping(const std::string& n, const std::string& k, ButtonType t, 
                  const std::string& g, uint8_t r, uint8_t c)
        : name(n), key(k), type(t), group(g), row(r), col(c) {}
};

// Real MS2000 button mappings from service data and front panel
static const std::vector<ButtonMapping> MS2000_BUTTON_MAPPINGS = {
    // Navigation/System buttons
    {"GLOBAL", "GLOBAL", ButtonType::MOMENTARY, "sys", 0, 0},
    {"EDIT", "EDIT", ButtonType::MOMENTARY, "sys", 0, 1},
    {"EXIT", "EXIT", ButtonType::MOMENTARY, "sys", 0, 2},
    {"WRITE", "WRITE", ButtonType::MOMENTARY, "sys", 0, 3},
    {"PAGE", "PAGE", ButtonType::MOMENTARY, "ui", 0, 4},
    {"+ / YES", "YES", ButtonType::MOMENTARY, "ui", 0, 5},
    {"- / NO", "NO", ButtonType::MOMENTARY, "ui", 0, 6},
    {"BANK UP", "BANK_UP", ButtonType::MOMENTARY, "bank", 0, 7},
    {"BANK DOWN", "BANK_DOWN", ButtonType::MOMENTARY, "bank", 1, 0},
    
    // Function buttons
    {"TIMBRE SELECT", "TIMBRE", ButtonType::LATCH2, "timbre", 1, 1},
    {"MOD SEQ ON/OFF", "SEQ_ENABLE", ButtonType::TOGGLE, "seq", 1, 2},
    {"MOD SEQ REC", "SEQ_REC", ButtonType::MOMENTARY, "seq", 1, 3},
    {"SEQ EDIT / CH PARAM SELECT", "SEQ_EDIT", ButtonType::LATCH3, "seq", 1, 4},
    {"LFO1 SELECT", "LFO_SEL1", ButtonType::LATCH2, "lfo", 1, 5},
    {"LFO2 SELECT", "LFO_SEL2", ButtonType::LATCH2, "lfo", 1, 6},
    {"EFFECT MOD/DELAY", "FX_MODE", ButtonType::LATCH2, "fx", 1, 7},
    {"ARPEGGIATOR ON/OFF", "ARP_ON", ButtonType::TOGGLE, "arp", 2, 0},
    {"ARPEGGIATOR LATCH", "ARP_LATCH", ButtonType::TOGGLE, "arp", 2, 1},
    
    // 16 Bank buttons (from front panel image)
    {"COMMON", "BANK_COMMON", ButtonType::MOMENTARY, "bank", 2, 2},
    {"VOICE", "BANK_VOICE", ButtonType::MOMENTARY, "bank", 2, 3},
    {"PITCH", "BANK_PITCH", ButtonType::MOMENTARY, "bank", 2, 4},
    {"OSC 1", "BANK_OSC1", ButtonType::MOMENTARY, "bank", 2, 5},
    {"OSC 2", "BANK_OSC2", ButtonType::MOMENTARY, "bank", 2, 6},
    {"FILTER", "BANK_FILTER", ButtonType::MOMENTARY, "bank", 2, 7},
    {"AMP", "BANK_AMP", ButtonType::MOMENTARY, "bank", 3, 0},
    {"EG", "BANK_EG", ButtonType::MOMENTARY, "bank", 3, 1},
    {"LFO", "BANK_LFO", ButtonType::MOMENTARY, "bank", 3, 2},
    {"PATCH", "BANK_PATCH", ButtonType::MOMENTARY, "bank", 3, 3},
    {"CH LEVEL", "BANK_CH_LEVEL", ButtonType::MOMENTARY, "bank", 3, 4},
    {"SEQ CH PAN", "BANK_SEQ_CH_PAN", ButtonType::MOMENTARY, "bank", 3, 5},
    {"MOD FX", "BANK_MOD_FX", ButtonType::MOMENTARY, "bank", 3, 6},
    {"DELAY FX", "BANK_DELAY_FX", ButtonType::MOMENTARY, "bank", 3, 7},
    {"ARPEGGIO", "BANK_ARPEGGIO", ButtonType::MOMENTARY, "bank", 4, 0},
    {"UTILITY", "BANK_UTILITY", ButtonType::MOMENTARY, "bank", 4, 1}
};

// ===== LED Matrix Mapping (8x8) =====

// LED mapping structure
struct LEDMapping {
    std::string name;
    uint8_t row;
    uint8_t col;
    
    LEDMapping(const std::string& n, uint8_t r, uint8_t c) : name(n), row(r), col(c) {}
};

// Real MS2000 LED mappings from service data
static const std::vector<LEDMapping> MS2000_LED_MAPPINGS = {
    {"SEQ2/PAN", 0, 0},
    {"MOD SEQ ON/OFF", 0, 1},
    {"OCT DOWN", 0, 2},
    {"SEQ3", 0, 3},
    {"SEQ1 LEVEL", 0, 4},
    {"TIMBRE SELECT 2", 0, 5},
    {"TIMBRE SELECT 1", 0, 6},
    {"LFO1 SQUARE", 0, 7},
    {"LFO1 TRI", 1, 0},
    {"LFO1 S/H", 1, 1},
    {"LFO2 SIN", 1, 2},
    {"LFO2 S/H", 1, 3},
    {"VP SOURCE MIDI2", 1, 4},
    {"VP DEST FREQ", 1, 5},
    {"VP SOURCE MIDI1", 1, 6},
    {"VP DEST PAN", 1, 7},
    {"VP DEST AMP", 2, 0},
    {"VP DEST VELOCITY", 2, 1},
    {"VP DEST CUTOFF", 2, 2},
    {"VP MOD4", 2, 3},
    {"VP MOD3", 2, 4},
    {"VP MOD2", 2, 5},
    {"VP MOD1", 2, 6},
    {"VP SOURCE EG1", 2, 7}
};

// ===== LCD Display Mapping =====

// LCD pin mapping
enum class LCDPin : uint8_t {
    VSS = 1, VDD = 2, VO = 3, RS = 4, RW = 5, E = 6,
    DB0 = 7, DB1 = 8, DB2 = 9, DB3 = 10, DB4 = 11, DB5 = 12, DB6 = 13, DB7 = 14,
    LED_PLUS = 15, LED_MINUS = 16
};

// LCD signal mapping
struct LCDSignalMapping {
    LCDPin pin;
    std::string signal;
    
    LCDSignalMapping(LCDPin p, const std::string& s) : pin(p), signal(s) {}
};

// Real MS2000 LCD pin mappings
static const std::vector<LCDSignalMapping> MS2000_LCD_PINS = {
    {LCDPin::VSS, "VSS"},
    {LCDPin::VDD, "VDD"},
    {LCDPin::VO, "VO"},
    {LCDPin::RS, "RS"},
    {LCDPin::RW, "RW"},
    {LCDPin::E, "E"},
    {LCDPin::DB0, "DB0"},
    {LCDPin::DB1, "DB1"},
    {LCDPin::DB2, "DB2"},
    {LCDPin::DB3, "DB3"},
    {LCDPin::DB4, "DB4"},
    {LCDPin::DB5, "DB5"},
    {LCDPin::DB6, "DB6"},
    {LCDPin::DB7, "DB7"},
    {LCDPin::LED_PLUS, "LED+"},
    {LCDPin::LED_MINUS, "LED-"}
};

// ===== Hardware Mapping Functions =====

// Get knob mapping by bank and mux
const KnobMapping* getKnobMapping(ADCBank bank, ADCMux mux);

// Get knob role based on current hardware state
std::string getKnobRole(const KnobMapping& knob, const HardwareState& state);

// Get button mapping by row and column
const ButtonMapping* getButtonMapping(uint8_t row, uint8_t col);

// Get LED mapping by name
const LEDMapping* getLEDMapping(const std::string& name);

// Get LED mapping by position
const LEDMapping* getLEDMapping(uint8_t row, uint8_t col);

// Convert ADC bank/mux to knob index
uint8_t adcToKnobIndex(ADCBank bank, ADCMux mux);

// Convert knob index to ADC bank/mux
void knobIndexToADC(uint8_t knobIndex, ADCBank& bank, ADCMux& mux);

} // namespace MS2000
