#pragma once

#include "core/h8s2350_emulator.h"
#include "core/dsp56362_emulator.h"
#include <vector>
#include <string>
#include <iostream>

namespace MS2000 {

// Potenciométer mapping struktúra
struct PotMap {
    std::string name;
    int adc_group;    // ADC csoport (4-7)
    int adc_channel;  // ADC csatorna (0-7)
};

// Gomb mapping struktúra  
struct ButtonMap {
    std::string name;
    int row;     // Switch matrix sor (0-3)
    int col;     // Switch matrix oszlop (0-7)
};

// MS-2000 port figyelő osztály
class MS2000PortMonitor {
private:
    H8S2350Emulator* m_mcu;
    DSP56362Emulator* m_dsp;
    
    // Potenciométer és gomb mapping-ok
    std::vector<PotMap> m_potMaps;
    std::vector<ButtonMap> m_buttonMaps;
    
    // Debug flag
    bool m_debug;
    
    // GUI integration support
    uint8_t m_lastRow;
    uint8_t m_lastLedRow;
    uint8_t m_lastLedCols;
    bool m_ledMatrix[8][8];
    bool m_buttonMatrix[8][8];
    uint16_t m_potentiometers[35];

public:
    MS2000PortMonitor(H8S2350Emulator* mcu, DSP56362Emulator* dsp, bool debug = true);
    ~MS2000PortMonitor() = default;
    
    // Inicializálás
    bool initialize();
    
    // Potenciométer változás kezelése
    void onPotentiometerChange(int adc_group, int adc_channel, uint16_t value);
    void onPotentiometerChangeByName(const std::string& name, uint16_t value);
    
    // Gomb nyomás/felengedés kezelése
    void onButtonPress(int row, int col, bool pressed);
    void onButtonPressByName(const std::string& name, bool pressed);
    
    // Tesztelő funkciók
    void testAllPotentiometers();
    void testAllButtons();
    void testSpecificPotentiometer(const std::string& name);
    void testSpecificButton(const std::string& name);
    
    // Debug funkciók
    void dumpPotentiometerMappings();
    void dumpButtonMappings();
    void setDebug(bool debug) { m_debug = debug; }
    
    // GUI integration methods
    void setPotentiometer(int pot, uint16_t value);
    uint16_t readADC();
    void setADCMuxChannel(int channel);
    bool getLED(int row, int col);
    void setButton(int row, int col, bool pressed);
    uint8_t readPort(uint16_t address);
    void writePort(uint16_t address, uint8_t value);
    uint8_t getLastRow() const { return m_lastRow; }
    uint8_t getLastLedRow() const { return m_lastLedRow; }
    uint8_t getLastLedCols() const { return m_lastLedCols; }
    
    // Additional GUI methods
    uint8_t readButtonMatrix();
    void setLEDRowSelect(uint8_t row);
    void setLEDColumnMask(uint8_t mask);
    const std::vector<PotMap>& getPotentiometerMaps() const { return m_potMaps; }
    const std::vector<ButtonMap>& getButtonMaps() const { return m_buttonMaps; }

private:
    // Helper funkciók
    PotMap* findPotentiometerByName(const std::string& name);
    ButtonMap* findButtonByName(const std::string& name);
    void logMessage(const std::string& message);
};

// MS-2000 potenciométer mapping-ok
std::vector<PotMap> ms2k_pots_vrad();

// MS-2000 gomb mapping-ok
std::vector<ButtonMap> ms2k_buttons_sw_matrix();

} // namespace MS2000
