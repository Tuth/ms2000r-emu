#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <map>

namespace MS2000 {

// MS2000 Knob Mapping System
// Based on 35 physical knobs + multifunctional lower 16 knobs
// 
// The lower 16 knobs (17-32) have dual function:
// - Normal mode: EG1, EG2, LFO, FX, Virtual Patch parameters
// - Step Seq mode: Step Sequencer 1/2/3 lane values

enum class KnobMode {
    PARAM_EDIT = 0,    // Normal parameter mode (EG/LFO/FX/Patch)
    SEQ1_EDIT = 1,     // Step Sequencer 1 mode
    SEQ2_EDIT = 2,     // Step Sequencer 2 mode
    SEQ3_EDIT = 3      // Step Sequencer 3 mode
};

struct KnobDefinition {
    uint8_t knob_id;           // Physical knob ID (0-34)
    std::string name;          // Knob name
    std::string category;      // Category (OSC1, OSC2, MIXER, FILTER, etc.)
    uint8_t default_value;     // Default value (0-127)
    uint8_t min_value;         // Minimum value
    uint8_t max_value;         // Maximum value
    bool is_multifunctional;   // True for lower 16 knobs (17-32)
    
    // Multifunctional mappings (only for knobs 17-32)
    std::string param_mode_name;   // Normal mode parameter name
    std::string seq1_mode_name;    // SEQ1 mode parameter name
    std::string seq2_mode_name;    // SEQ2 mode parameter name
    std::string seq3_mode_name;    // SEQ3 mode parameter name
};

class MS2000KnobMapping {
private:
    std::array<KnobDefinition, 35> m_knob_definitions;
    std::map<std::string, uint8_t> m_knob_name_to_id;
    KnobMode m_current_mode;
    std::array<uint8_t, 35> m_knob_values;
    std::array<uint8_t, 16> m_step_seq_values;  // Step sequencer values
    
public:
    MS2000KnobMapping();
    ~MS2000KnobMapping() = default;
    
    // Knob access
    uint8_t getKnobValue(uint8_t knob_id) const;
    void setKnobValue(uint8_t knob_id, uint8_t value);
    
    // Multifunctional knob access
    uint8_t getMultifunctionalKnobValue(uint8_t knob_id, KnobMode mode) const;
    void setMultifunctionalKnobValue(uint8_t knob_id, KnobMode mode, uint8_t value);
    
    // Mode control
    KnobMode getCurrentMode() const { return m_current_mode; }
    void setCurrentMode(KnobMode mode) { m_current_mode = mode; }
    
    // Step sequencer access
    uint8_t getStepSeqValue(uint8_t step, uint8_t lane) const;
    void setStepSeqValue(uint8_t step, uint8_t lane, uint8_t value);
    
    // Knob information
    const KnobDefinition& getKnobDefinition(uint8_t knob_id) const;
    uint8_t getKnobIdByName(const std::string& name) const;
    std::string getKnobNameById(uint8_t knob_id) const;
    
    // ADC channel mapping (for MP stub)
    uint8_t knobIdToADCChannel(uint8_t knob_id) const;
    uint8_t adcChannelToKnobId(uint8_t adc_channel) const;
    
    // Reset all knobs to default values
    void resetToDefaults();
    
    // Debug and status
    void printKnobStatus() const;
    void printStepSeqStatus() const;
    
private:
    void initializeKnobDefinitions();
    void buildNameToIdMap();
};

} // namespace MS2000
