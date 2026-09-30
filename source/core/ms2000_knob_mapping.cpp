#include "ms2000_knob_mapping.h"
#include <iostream>
#include <algorithm>

namespace MS2000 {

// ==== Constructor ====

MS2000KnobMapping::MS2000KnobMapping()
    : m_current_mode(KnobMode::PARAM_EDIT)
{
    // Initialize knob values to defaults
    m_knob_values.fill(64);  // Middle value (64)
    m_step_seq_values.fill(64);
    
    // Initialize knob definitions
    initializeKnobDefinitions();
    buildNameToIdMap();
    
    // Reset to default values
    resetToDefaults();
    
    std::cout << "MS2000 Knob Mapping initialized (35 physical knobs)" << std::endl;
}

// ==== Knob Access ====

uint8_t MS2000KnobMapping::getKnobValue(uint8_t knob_id) const
{
    if (knob_id >= 35) {
        return 64;  // Default value for invalid knob ID
    }
    
    // Check if this is a multifunctional knob (17-32)
    if (knob_id >= 16 && knob_id <= 31) {
        return getMultifunctionalKnobValue(knob_id, m_current_mode);
    }
    
    return m_knob_values[knob_id];
}

void MS2000KnobMapping::setKnobValue(uint8_t knob_id, uint8_t value)
{
    if (knob_id >= 35) {
        return;  // Invalid knob ID
    }
    
    // Clamp value to valid range
    value = std::clamp(value, (uint8_t)0, (uint8_t)127);
    
    // Check if this is a multifunctional knob (17-32)
    if (knob_id >= 16 && knob_id <= 31) {
        setMultifunctionalKnobValue(knob_id, m_current_mode, value);
    } else {
        m_knob_values[knob_id] = value;
    }
}

// ==== Multifunctional Knob Access ====

uint8_t MS2000KnobMapping::getMultifunctionalKnobValue(uint8_t knob_id, KnobMode mode) const
{
    if (knob_id < 16 || knob_id > 31) {
        return 64;  // Not a multifunctional knob
    }
    
    switch (mode) {
        case KnobMode::PARAM_EDIT:
            return m_knob_values[knob_id];
            
        case KnobMode::SEQ1_EDIT:
        case KnobMode::SEQ2_EDIT:
        case KnobMode::SEQ3_EDIT: {
            // Map knob to step sequencer value
            uint8_t step = knob_id - 16;  // Convert to 0-15 step range
            return m_step_seq_values[step];
        }
            
        default:
            return 64;
    }
}

void MS2000KnobMapping::setMultifunctionalKnobValue(uint8_t knob_id, KnobMode mode, uint8_t value)
{
    if (knob_id < 16 || knob_id > 31) {
        return;  // Not a multifunctional knob
    }
    
    // Clamp value to valid range
    value = std::clamp(value, (uint8_t)0, (uint8_t)127);
    
    switch (mode) {
        case KnobMode::PARAM_EDIT:
            m_knob_values[knob_id] = value;
            break;
            
        case KnobMode::SEQ1_EDIT:
        case KnobMode::SEQ2_EDIT:
        case KnobMode::SEQ3_EDIT: {
            // Map knob to step sequencer value
            uint8_t step = knob_id - 16;  // Convert to 0-15 step range
            m_step_seq_values[step] = value;
            break;
        }
    }
}

// ==== Step Sequencer Access ====

uint8_t MS2000KnobMapping::getStepSeqValue(uint8_t step, uint8_t lane) const
{
    if (step >= 16 || lane >= 3) {
        return 64;  // Invalid step or lane
    }
    
    // For now, all lanes share the same values
    // In a full implementation, each lane would have separate values
    return m_step_seq_values[step];
}

void MS2000KnobMapping::setStepSeqValue(uint8_t step, uint8_t lane, uint8_t value)
{
    if (step >= 16 || lane >= 3) {
        return;  // Invalid step or lane
    }
    
    // Clamp value to valid range
    value = std::clamp(value, (uint8_t)0, (uint8_t)127);
    
    // For now, all lanes share the same values
    // In a full implementation, each lane would have separate values
    m_step_seq_values[step] = value;
}

// ==== Knob Information ====

const KnobDefinition& MS2000KnobMapping::getKnobDefinition(uint8_t knob_id) const
{
    static KnobDefinition invalid_knob = {255, "INVALID", "INVALID", 64, 0, 127, false};
    
    if (knob_id >= 35) {
        return invalid_knob;
    }
    
    return m_knob_definitions[knob_id];
}

uint8_t MS2000KnobMapping::getKnobIdByName(const std::string& name) const
{
    auto it = m_knob_name_to_id.find(name);
    return (it != m_knob_name_to_id.end()) ? it->second : 255;
}

std::string MS2000KnobMapping::getKnobNameById(uint8_t knob_id) const
{
    if (knob_id >= 35) {
        return "INVALID";
    }
    
    return m_knob_definitions[knob_id].name;
}

// ==== ADC Channel Mapping ====

uint8_t MS2000KnobMapping::knobIdToADCChannel(uint8_t knob_id) const
{
    // Map knob ID to ADC channel
    // This mapping depends on the actual hardware design
    // For now, use a simple 1:1 mapping
    return knob_id;
}

uint8_t MS2000KnobMapping::adcChannelToKnobId(uint8_t adc_channel) const
{
    // Map ADC channel to knob ID
    // This mapping depends on the actual hardware design
    // For now, use a simple 1:1 mapping
    return (adc_channel < 35) ? adc_channel : 255;
}

// ==== Reset and Debug ====

void MS2000KnobMapping::resetToDefaults()
{
    for (uint8_t i = 0; i < 35; i++) {
        m_knob_values[i] = m_knob_definitions[i].default_value;
    }
    
    m_step_seq_values.fill(64);  // Middle value for step sequencer
    m_current_mode = KnobMode::PARAM_EDIT;
    
    std::cout << "MS2000 Knob Mapping reset to defaults" << std::endl;
}

void MS2000KnobMapping::printKnobStatus() const
{
    std::cout << "=== MS2000 Knob Status ===" << std::endl;
    std::cout << "Current Mode: " << static_cast<int>(m_current_mode) << std::endl;
    
    for (uint8_t i = 0; i < 35; i++) {
        const auto& def = m_knob_definitions[i];
        uint8_t value = getKnobValue(i);
        std::cout << "Knob " << (int)i << " (" << def.name << "): " << (int)value << std::endl;
    }
}

void MS2000KnobMapping::printStepSeqStatus() const
{
    std::cout << "=== Step Sequencer Status ===" << std::endl;
    std::cout << "Current Mode: " << static_cast<int>(m_current_mode) << std::endl;
    
    for (uint8_t step = 0; step < 16; step++) {
        uint8_t value = m_step_seq_values[step];
        std::cout << "Step " << (int)step << ": " << (int)value << std::endl;
    }
}

// ==== Private Methods ====

void MS2000KnobMapping::initializeKnobDefinitions()
{
    // OSCILLATOR 1
    m_knob_definitions[0] = {0, "OSC1 Control 1", "OSC1", 64, 0, 127, false, "", "", "", ""};
    m_knob_definitions[1] = {1, "OSC1 Control 2", "OSC1", 64, 0, 127, false, "", "", "", ""};
    m_knob_definitions[2] = {2, "OSC1 Mod Depth", "OSC1", 0, 0, 127, false, "", "", "", ""};
    
    // OSCILLATOR 2
    m_knob_definitions[3] = {3, "OSC2 Control 1", "OSC2", 64, 0, 127, false, "", "", "", ""};
    m_knob_definitions[4] = {4, "OSC2 Control 2", "OSC2", 64, 0, 127, false, "", "", "", ""};
    m_knob_definitions[5] = {5, "OSC2 Pitch", "OSC2", 64, 0, 127, false, "", "", "", ""};
    m_knob_definitions[6] = {6, "OSC2 Fine Tune", "OSC2", 64, 0, 127, false, "", "", "", ""};
    
    // MIXER
    m_knob_definitions[7] = {7, "OSC1 Level", "MIXER", 127, 0, 127, false, "", "", "", ""};
    m_knob_definitions[8] = {8, "OSC2 Level", "MIXER", 127, 0, 127, false, "", "", "", ""};
    m_knob_definitions[9] = {9, "Noise Level", "MIXER", 0, 0, 127, false, "", "", "", ""};
    
    // FILTER
    m_knob_definitions[10] = {10, "Cutoff", "FILTER", 127, 0, 127, false, "", "", "", ""};
    m_knob_definitions[11] = {11, "Resonance", "FILTER", 0, 0, 127, false, "", "", "", ""};
    m_knob_definitions[12] = {12, "EG Intensity", "FILTER", 64, 0, 127, false, "", "", "", ""};
    m_knob_definitions[13] = {13, "Keyboard Track", "FILTER", 64, 0, 127, false, "", "", "", ""};
    
    // AMP
    m_knob_definitions[14] = {14, "Level", "AMP", 127, 0, 127, false, "", "", "", ""};
    m_knob_definitions[15] = {15, "Pan", "AMP", 64, 0, 127, false, "", "", "", ""};
    
    // EG1 (Filter Envelope) - Multifunctional knobs 16-19
    m_knob_definitions[16] = {16, "EG1 Attack", "EG1", 0, 0, 127, true, "EG1 Attack", "SEQ1 Level", "SEQ2 Level", "SEQ3 Level"};
    m_knob_definitions[17] = {17, "EG1 Decay", "EG1", 64, 0, 127, true, "EG1 Decay", "SEQ1 Pan", "SEQ2 Pan", "SEQ3 Pan"};
    m_knob_definitions[18] = {18, "EG1 Sustain", "EG1", 64, 0, 127, true, "EG1 Sustain", "SEQ1 Pitch", "SEQ2 Pitch", "SEQ3 Pitch"};
    m_knob_definitions[19] = {19, "EG1 Release", "EG1", 64, 0, 127, true, "EG1 Release", "SEQ1 Assign", "SEQ2 Assign", "SEQ3 Assign"};
    
    // EG2 (Amp Envelope) - Multifunctional knobs 20-23
    m_knob_definitions[20] = {20, "EG2 Attack", "EG2", 0, 0, 127, true, "EG2 Attack", "SEQ1 Level", "SEQ2 Level", "SEQ3 Level"};
    m_knob_definitions[21] = {21, "EG2 Decay", "EG2", 64, 0, 127, true, "EG2 Decay", "SEQ1 Pan", "SEQ2 Pan", "SEQ3 Pan"};
    m_knob_definitions[22] = {22, "EG2 Sustain", "EG2", 64, 0, 127, true, "EG2 Sustain", "SEQ1 Pitch", "SEQ2 Pitch", "SEQ3 Pitch"};
    m_knob_definitions[23] = {23, "EG2 Release", "EG2", 64, 0, 127, true, "EG2 Release", "SEQ1 Assign", "SEQ2 Assign", "SEQ3 Assign"};
    
    // LFO1 - Multifunctional knobs 24-25
    m_knob_definitions[24] = {24, "LFO1 Frequency", "LFO1", 64, 0, 127, true, "LFO1 Frequency", "SEQ1 Level", "SEQ2 Level", "SEQ3 Level"};
    m_knob_definitions[25] = {25, "LFO1 Tempo Sync", "LFO1", 64, 0, 127, true, "LFO1 Tempo Sync", "SEQ1 Pan", "SEQ2 Pan", "SEQ3 Pan"};
    
    // LFO2 - Multifunctional knobs 26-27
    m_knob_definitions[26] = {26, "LFO2 Frequency", "LFO2", 64, 0, 127, true, "LFO2 Frequency", "SEQ1 Pitch", "SEQ2 Pitch", "SEQ3 Pitch"};
    m_knob_definitions[27] = {27, "LFO2 Tempo Sync", "LFO2", 64, 0, 127, true, "LFO2 Tempo Sync", "SEQ1 Assign", "SEQ2 Assign", "SEQ3 Assign"};
    
    // EFFECTS - Multifunctional knobs 28-29
    m_knob_definitions[28] = {28, "Effect Speed", "EFFECTS", 64, 0, 127, true, "Effect Speed", "SEQ1 Level", "SEQ2 Level", "SEQ3 Level"};
    m_knob_definitions[29] = {29, "Effect Depth", "EFFECTS", 64, 0, 127, true, "Effect Depth", "SEQ1 Pan", "SEQ2 Pan", "SEQ3 Pan"};
    
    // VIRTUAL PATCH - Multifunctional knobs 30-33
    m_knob_definitions[30] = {30, "Patch 1 Amount", "VIRTUAL_PATCH", 0, 0, 127, true, "Patch 1 Amount", "SEQ1 Pitch", "SEQ2 Pitch", "SEQ3 Pitch"};
    m_knob_definitions[31] = {31, "Patch 2 Amount", "VIRTUAL_PATCH", 0, 0, 127, true, "Patch 2 Amount", "SEQ1 Assign", "SEQ2 Assign", "SEQ3 Assign"};
    m_knob_definitions[32] = {32, "Patch 3 Amount", "VIRTUAL_PATCH", 0, 0, 127, true, "Patch 3 Amount", "SEQ1 Level", "SEQ2 Level", "SEQ3 Level"};
    m_knob_definitions[33] = {33, "Patch 4 Amount", "VIRTUAL_PATCH", 0, 0, 127, true, "Patch 4 Amount", "SEQ1 Pan", "SEQ2 Pan", "SEQ3 Pan"};
    
    // GLOBAL / MASTER
    m_knob_definitions[34] = {34, "Master Volume", "GLOBAL", 127, 0, 127, false, "", "", "", ""};
}

void MS2000KnobMapping::buildNameToIdMap()
{
    for (uint8_t i = 0; i < 35; i++) {
        m_knob_name_to_id[m_knob_definitions[i].name] = i;
    }
}

} // namespace MS2000
