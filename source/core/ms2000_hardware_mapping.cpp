#include "ms2000_hardware_mapping.h"
#include <algorithm>
#include <stdexcept>

namespace MS2000 {

// ===== Hardware Mapping Function Implementations =====

// Get knob mapping by bank and mux
const KnobMapping* getKnobMapping(ADCBank bank, ADCMux mux) {
    for (const auto& knob : MS2000_KNOB_MAPPINGS) {
        if (knob.bank == bank && knob.mux == mux) {
            return &knob;
        }
    }
    return nullptr;
}

// Get knob role based on current hardware state
std::string getKnobRole(const KnobMapping& knob, const HardwareState& state) {
    // Check multifunction roles first
    for (const auto& multiRole : knob.multiRoles) {
        const std::string& condition = multiRole.first;
        const std::string& role = multiRole.second;
        
        // Parse condition and check state
        if (condition == "LFO=2" && state.lfo == LFOSelect::LFO_2) {
            return role;
        }
        if (condition == "FX=DELAY" && state.fx == FXMode::DELAY) {
            return role;
        }
        if (condition == "TIMBRE=2" && state.timbre == TimbreSelect::TIMBRE_2) {
            return role;
        }
        if (condition == "SEQ=ON" && state.seq == SeqMode::SEQ_ON) {
            return role;
        }
        if (condition.find("SEQ=ON") != std::string::npos && state.seq == SeqMode::SEQ_ON) {
            // Handle sequencer conditions with lane specification
            if (condition.find("LANE=SEQ1_LEVEL") != std::string::npos && state.lane == SeqLane::LANE_LEVEL) {
                return role;
            }
            if (condition.find("LANE=SEQ2_PAN") != std::string::npos && state.lane == SeqLane::LANE_PAN) {
                return role;
            }
            if (condition.find("LANE=SEQ3_PITCH") != std::string::npos && state.lane == SeqLane::LANE_PITCH) {
                return role;
            }
        }
    }
    
    // Return base role if no multifunction condition matches
    return knob.baseRole;
}

// Get button mapping by row and column
const ButtonMapping* getButtonMapping(uint8_t row, uint8_t col) {
    for (const auto& button : MS2000_BUTTON_MAPPINGS) {
        if (button.row == row && button.col == col) {
            return &button;
        }
    }
    return nullptr;
}

// Get LED mapping by name
const LEDMapping* getLEDMapping(const std::string& name) {
    for (const auto& led : MS2000_LED_MAPPINGS) {
        if (led.name == name) {
            return &led;
        }
    }
    return nullptr;
}

// Get LED mapping by position
const LEDMapping* getLEDMapping(uint8_t row, uint8_t col) {
    for (const auto& led : MS2000_LED_MAPPINGS) {
        if (led.row == row && led.col == col) {
            return &led;
        }
    }
    return nullptr;
}

// Convert ADC bank/mux to knob index
uint8_t adcToKnobIndex(ADCBank bank, ADCMux mux) {
    // Handle analog-only controls (no ADC bank/mux)
    if (bank == ADCBank::BANK_NONE) {
        switch (static_cast<uint8_t>(mux)) {
            case 0: return 0;  // POWER VOLUME
            case 1: return 1;  // AUDIO IN 1 LEVEL
            case 2: return 2;  // AUDIO IN 2 LEVEL
            default: throw std::invalid_argument("Invalid analog-only mux");
        }
    }
    
    uint8_t baseIndex = 0;
    
    // Calculate base index based on bank
    switch (bank) {
        case ADCBank::BANK_4: baseIndex = 3; break;   // 8 knobs (3-10)
        case ADCBank::BANK_5: baseIndex = 11; break;  // 7 knobs (11-17)
        case ADCBank::BANK_6: baseIndex = 18; break;  // 7 knobs (18-24)
        case ADCBank::BANK_7: baseIndex = 25; break;  // 9 knobs (25-33)
        case ADCBank::BANK_8: baseIndex = 34; break;  // 8 knobs (34-41)
        case ADCBank::BANK_9: baseIndex = 42; break;  // 8 knobs (42-49)
        default: throw std::invalid_argument("Invalid ADC bank");
    }
    
    return baseIndex + static_cast<uint8_t>(mux);
}

// Convert knob index to ADC bank/mux
void knobIndexToADC(uint8_t knobIndex, ADCBank& bank, ADCMux& mux) {
    if (knobIndex < 3) {
        // Analog-only controls
        bank = ADCBank::BANK_NONE;
        mux = static_cast<ADCMux>(knobIndex);
    } else if (knobIndex < 11) {
        bank = ADCBank::BANK_4;
        mux = static_cast<ADCMux>(knobIndex - 3);
    } else if (knobIndex < 18) {
        bank = ADCBank::BANK_5;
        mux = static_cast<ADCMux>(knobIndex - 11);
    } else if (knobIndex < 25) {
        bank = ADCBank::BANK_6;
        mux = static_cast<ADCMux>(knobIndex - 18);
    } else if (knobIndex < 34) {
        bank = ADCBank::BANK_7;
        mux = static_cast<ADCMux>(knobIndex - 25);
    } else if (knobIndex < 42) {
        bank = ADCBank::BANK_8;
        mux = static_cast<ADCMux>(knobIndex - 34);
    } else if (knobIndex < 50) {
        bank = ADCBank::BANK_9;
        mux = static_cast<ADCMux>(knobIndex - 42);
    } else {
        throw std::invalid_argument("Invalid knob index");
    }
}

// ===== Additional Utility Functions =====

// Get all knobs for a specific ADC bank
std::vector<const KnobMapping*> getKnobsForBank(ADCBank bank) {
    std::vector<const KnobMapping*> knobs;
    for (const auto& knob : MS2000_KNOB_MAPPINGS) {
        if (knob.bank == bank) {
            knobs.push_back(&knob);
        }
    }
    return knobs;
}

// Get all buttons for a specific group
std::vector<const ButtonMapping*> getButtonsForGroup(const std::string& group) {
    std::vector<const ButtonMapping*> buttons;
    for (const auto& button : MS2000_BUTTON_MAPPINGS) {
        if (button.group == group) {
            buttons.push_back(&button);
        }
    }
    return buttons;
}

// Get all LEDs for a specific row
std::vector<const LEDMapping*> getLEDsForRow(uint8_t row) {
    std::vector<const LEDMapping*> leds;
    for (const auto& led : MS2000_LED_MAPPINGS) {
        if (led.row == row) {
            leds.push_back(&led);
        }
    }
    return leds;
}

// Get knob name by ADC bank and mux
std::string getKnobName(ADCBank bank, ADCMux mux) {
    const KnobMapping* mapping = getKnobMapping(bank, mux);
    return mapping ? mapping->name : "Unknown";
}

// Get button name by row and column
std::string getButtonName(uint8_t row, uint8_t col) {
    const ButtonMapping* mapping = getButtonMapping(row, col);
    return mapping ? mapping->name : "Unknown";
}

// Get LED name by row and column
std::string getLEDName(uint8_t row, uint8_t col) {
    const LEDMapping* mapping = getLEDMapping(row, col);
    return mapping ? mapping->name : "Unknown";
}

// Check if a knob has multifunction behavior
bool isKnobMultifunction(const KnobMapping& knob) {
    return !knob.multiRoles.empty();
}

// Get the number of knobs in the system
size_t getKnobCount() {
    return MS2000_KNOB_MAPPINGS.size();
}

// Get the number of buttons in the system
size_t getButtonCount() {
    return MS2000_BUTTON_MAPPINGS.size();
}

// Get the number of LEDs in the system
size_t getLEDCount() {
    return MS2000_LED_MAPPINGS.size();
}

// Get all knob names
std::vector<std::string> getAllKnobNames() {
    std::vector<std::string> names;
    for (const auto& knob : MS2000_KNOB_MAPPINGS) {
        names.push_back(knob.name);
    }
    return names;
}

// Get all button names
std::vector<std::string> getAllButtonNames() {
    std::vector<std::string> names;
    for (const auto& button : MS2000_BUTTON_MAPPINGS) {
        names.push_back(button.name);
    }
    return names;
}

// Get all LED names
std::vector<std::string> getAllLEDNames() {
    std::vector<std::string> names;
    for (const auto& led : MS2000_LED_MAPPINGS) {
        names.push_back(led.name);
    }
    return names;
}

} // namespace MS2000
