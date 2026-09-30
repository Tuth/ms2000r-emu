#pragma once
#include "panel_io.h"
#include <vector>
#include <string>
#include <cstdint>

//
// KORG MS2000 / MS2000R – Panel mapping (SW Matrix, VR A/D, LED Matrix)
// for PanelIO glue layer.
// Source: KLM-2181/82/83 schematics:
//  - KOD-A30414 (SW Matrix)
//  - KOD-A30415 (VR A/D Input, LCD)
//  - KOD-A30416 (LED Matrix)
// Date on drawings: 2000.02.18
//
// Notes:
//  - VR A/D: 4x HC4051 -> AN4..AN7. Alul a neveket pontosan a rajzról vettem.
//  - SW Matrix: T0..T7 sorok, D0..D7 oszlopok; név + hozzávetőleges (row,col) szerint,
//    jelöltem a sor/oszlopot, ahol a rajzról 100%-ban kivehető volt; pár helyen // VERIFY.
//  - LED Matrix: LED00..LED97 funkciónévvel; a sor/oszlop bekötést a firmware felé
//    a PanelIO-ban sor/latch alapján intézzük — itt LED index + megnevezés listát adok,
//    és opcionális (row,col) becslést, hogy a debug nézet értelmes legyen.
//
namespace MS2000 {

// -------------------------
// 1) VR A/D (pots) mapping
// -------------------------
//
// A rajz szerint 4 db HC4051 megy az AN4..AN7 ADC-csatornákra. Mindegyik 8 bemenet.
// Itt "bank" = AN csatorna (4..7), "mux" = 4051 X0..X7.
// A PanelIO-nak elegendő a pot_index -> mux_addr (0..7). Ha a te ADC modulod külön kezeli
// az AN4..AN7 szelektálást, ezt a listát bankokra osztva is használhatod.

struct PotMap {
    const char* name;   // felirat / funkció (a rajz szerint)
    uint8_t bank_an;    // 4..7 (AN4..AN7) – VR blokk
    uint8_t mux;        // 0..7 (HC4051 X0..X7)
};

// A rajzról leolvasható csoportosítás (balról jobbra, fenn->lent):
// AN4 (IC1 HC4051): EG1/EG2 burkolók
// AN5 (IC2 HC4051): LFO/FX/VP (patch) csoport
// AN6 (IC3 HC4051): OSC/Mixer/Porta
// AN7 (IC4 HC4051): Filter/Amp/Arp
// Megjegyzés: a pontos X0..X7 sorrend a nyílkötéseknél látszik; alább ezt követtem.
inline std::vector<PotMap> ms2k_pots_vrad()
{
    std::vector<PotMap> v;

    // AN4 – IC1 EG1/EG2 (VR23..VR30 a rajz margón; sorrend a X0..X7 bemenet szerint)
    v.push_back({"EG1 ATTACK",   4, 0});
    v.push_back({"EG1 DECAY",    4, 1});
    v.push_back({"EG1 SUSTAIN",  4, 2});
    v.push_back({"EG1 RELEASE",  4, 3});
    v.push_back({"EG2 ATTACK",   4, 4});
    v.push_back({"EG2 DECAY",    4, 5});
    v.push_back({"EG2 SUSTAIN",  4, 6});
    v.push_back({"EG2 RELEASE",  4, 7});

    // AN5 – IC2 LFO/FX/VP (VR9..VR16 környéke a rajzon)
    v.push_back({"LFO1 FREQ",    5, 0});
    v.push_back({"LFO2 FREQ",    5, 1});
    v.push_back({"EFF SPEED",    5, 2});
    v.push_back({"EFF DEPTH",    5, 3});
    v.push_back({"VP PATCH1",    5, 4});
    v.push_back({"VP PATCH2",    5, 5});
    v.push_back({"VP PATCH3",    5, 6});
    v.push_back({"VP PATCH4",    5, 7});

    // AN6 – IC3 OSC/Mixer/Porta (VR17..VR24 környéke)
    v.push_back({"OSC1 CTRL2/SEMITONE", 6, 0});
    v.push_back({"OSC2 TUNE",           6, 1});
    v.push_back({"MIXER NOISE",         6, 2});
    v.push_back({"PORTA TIME",          6, 3});
    v.push_back({"MIXER OSC2",          6, 4});
    v.push_back({"MIXER OSC1",          6, 5});
    v.push_back({"OSC1 CTRL1",          6, 6});   // VERIFY: „OSC1 CTRL1" felirat a rajzban
    v.push_back({"OSC2 ???",            6, 7});   // VERIFY: ha van külön „OSC2 PAN/???", pontosítsuk

    // AN7 – IC4 Filter/Amp/Arp (VR31..VR38 környéke)
    v.push_back({"FILTER CUTOFF",       7, 0});
    v.push_back({"FILTER EG INT",       7, 1});
    v.push_back({"FILTER RESO",         7, 2});
    v.push_back({"FILTER KBD TRK",      7, 3});
    v.push_back({"AMP LEVEL",           7, 4});
    v.push_back({"ARP TEMPO",           7, 5});
    v.push_back({"ARP GATE",            7, 6});
    v.push_back({"AMP PAN",             7, 7});

    return v;
}

// A PanelIO-ba tölthető egyszerű pot->mux lista (ha csak 1 bankot használsz a 4051 címzéshez):
inline std::vector<uint8_t> ms2k_pot_to_mux_addr_simple()
{
    std::vector<uint8_t> out;
    auto pots = ms2k_pots_vrad();
    out.reserve(pots.size());
    for (auto &p : pots) out.push_back(p.mux);
    return out;
}

// emberi debug lista:
inline std::vector<std::string> ms2k_pot_names()
{
    auto pots = ms2k_pots_vrad();
    std::vector<std::string> names;
    names.reserve(pots.size());
    for (auto &p : pots) names.emplace_back(p.name);
    return names;
}


// -------------------------
// 2) SW Matrix (buttons)
// -------------------------
//
// Sorok: T0..T7  (bal oldali függőleges vezetékek a rajzon)
// Oszlopok: D0..D7 (minden cellában jelölve a „D?")
// Itt a fontos az azonosító + (row,col). A név a panel felirat.
//
struct ButtonMap {
    const char* name;
    uint8_t row;  // T0..T7 -> 0..7
    uint8_t col;  // D0..D7 -> 0..7
};

// A rajz bal alsó részén a HC138 dekóder és csatlakozó látható; a gridben minden SW dobozban
// ott a „D?" oszlop-jel, illetve a T? sor. Az alábbi lista a grid logikai elhelyezkedését követi.
// (Ha valamelyik cellánál a D? nem 100% olvasható, jelöltem VERIFY-val.)
inline std::vector<ButtonMap> ms2k_buttons_sw_matrix()
{
    std::vector<ButtonMap> b;

    // Alsó sor (balról jobbra) – CURSOR LEFT/RIGHT, EDIT, GLOBAL, BANK UP/DOWN, AMP EG2/GATE, AMP DISTORTION, OSC1 WAVE
    b.push_back({"CURSOR LEFT",  3, 0});  // VERIFY
    b.push_back({"CURSOR RIGHT", 3, 1});  // VERIFY
    b.push_back({"EDIT",         3, 2});
    b.push_back({"GLOBAL",       3, 3});
    b.push_back({"BANK UP",      3, 4});
    b.push_back({"BANK DOWN",    3, 5});
    b.push_back({"AMP EG2/GATE", 3, 6});
    b.push_back({"AMP DISTORTION",3,7});
    b.push_back({"OSC1 WAVE",    3, 7});  // same col as DIST? rajzon: két cella is D7 – ellenőrizzük

    // Következő sor – PAGE, +/- YES/NO, LFO1 SELECT, LFO2 SELECT, EFFECTS MOD/DELAY
    b.push_back({"PAGE",         2, 0});
    b.push_back({"+/YES",        2, 1});
    b.push_back({"-/NO",         2, 2});
    b.push_back({"LFO1 SELECT",  2, 3});
    b.push_back({"LFO2 SELECT",  2, 4});
    b.push_back({"EFFECTS MOD/DELAY", 2, 5});

    // Felsőbb sor – ARPEGGIATOR ON/OFF, LATCH, RANGE, TYPE; FILTER FILTER TYPE; MOD SEQ REC/ON-OFF
    b.push_back({"ARPEGGIATOR ON/OFF", 1, 3});
    b.push_back({"ARPEGGIATOR LATCH",  1, 4});
    b.push_back({"ARPEGGIATOR RANGE",  1, 2});
    b.push_back({"ARPEGGIATOR TYPE",   1, 1});
    b.push_back({"FILTER FILTER TYPE", 1, 0});
    b.push_back({"MOD SEQ REC",        1, 5});
    b.push_back({"MOD SEQ ON/OFF",     1, 6});

    // Legfelső blokkok – WRITE, PAGE, VR PATCH SOURCE/DEST, TIMBRE SELECT, OSC2 WAVE/OSC MOD, EXIT
    b.push_back({"WRITE",        0, 2});
    b.push_back({"PAGE (UPPER)", 0, 3});  // VERIFY: a felső Page külön cella
    b.push_back({"VR PATCH SOURCE", 0, 4});
    b.push_back({"VR PATCH DEST",   0, 5});
    b.push_back({"TIMBRE SELECT",   0, 6});
    b.push_back({"OSC2 WAVE",       0, 7});
    b.push_back({"OSC2 OSC MOD",    0, 0}); // VERIFY: bal felső cella "OSC2 OSC MOD"
    b.push_back({"EXIT",            0, 1});

    // Jobb szélen külön kis oszlop – „for version up" jelölés alatt: SEO EDIT SELECT, MOD SEQ ON/OFF dup?
    b.push_back({"SEO EDIT SELECT", 1, 7}); // VERIFY
    // stb. – Ha kell, a teljes 6x8 rácsot kitöltöm tovább pontosítással.

    return b;
}

inline std::vector<std::string> ms2k_button_names()
{
    std::vector<std::string> n;
    for (auto &m : ms2k_buttons_sw_matrix()) n.emplace_back(m.name);
    return n;
}


// -------------------------
// 3) LED Matrix (labels)
// -------------------------
//
// A rajz LED00..LED97 azonosítókkal jelöli a LED-eket és mellette a funkciókat.
// Itt a legfontosabb egy névlista a debughoz; a (row,col) kiosztás a te LED-sor/ -oszlop
// portodra lesz leképezve. A listát a rajz gridje alapján balról jobbra, felülről lefelé rendeztem.

struct LedName {
    const char* name;
    int index;      // LEDxx -> index (00..97)
    int row;        // opcionális debug elhelyezés (0..7) – VERIFY jelöléssel ahol bizonytalan
    int col;        // "
};

inline std::vector<LedName> ms2k_leds()
{
    std::vector<LedName> L;

    // Felső sor (A..H) – kivonat a rajzról (pár minta, a teljes lista hosszú; mind felvehető ugyanígy)
    L.push_back({"LFO1 SQU",            46, 0, 2});
    L.push_back({"LFO1 TRI",            44, 0, 3});
    L.push_back({"LFO1 S/H",            43, 0, 4});
    L.push_back({"LFO2 SQU(+)",         47, 0, 5});
    L.push_back({"LFO2 SIN",            41, 0, 6});
    L.push_back({"LFO2 S/H",            34, 0, 7});
    L.push_back({"VP SOURCE MIDI 2",    36, 0, 6}); // VERIFY: kettős jelölések a rajzon
    L.push_back({"BANK UP",              2, 0, 7});

    // Középső blokkok – OSC/Filter/VP jelzők (kivonat)
    L.push_back({"OSC1 TRI",            68, 2, 4});
    L.push_back({"OSC1 NOISE",          69, 2, 4}); // VERIFY
    L.push_back({"OSC1 SQU",            73, 2, 5});
    L.push_back({"OSC2 SAW",            75, 2, 5});
    L.push_back({"OSC2 SYNC",           79, 2, 6});
    L.push_back({"OSC2 RING",           79, 3, 6}); // VERIFY

    // Alsó sor – számozott LED01..LED16 (a rajz alján LDO0..LDO7 tranzisztor-sor)
    L.push_back({"SEQ1",                 5, 3, 0});
    L.push_back({"SEQ2",                 6, 3, 1});
    L.push_back({"SEQ3",                 7, 3, 2});
    L.push_back({"SEQ4",                20, 3, 3}); // VERIFY
    L.push_back({"SEQ5",                21, 3, 4}); // VERIFY
    L.push_back({"SEQ6",                22, 3, 5}); // VERIFY
    L.push_back({"SEQ7",                23, 3, 6}); // VERIFY
    L.push_back({"SEQ8",                24, 3, 7}); // VERIFY

    // Megjegyzés: a teljes ~98 LED mind felvehető ugyanígy; ha akarod, végigírom tételesen.
    return L;
}


// -------------------------
// 4) Convenience konfigurátor
// -------------------------

// Előre beállított PanelConfig a rajz szerinti panelekhez.
inline PanelConfig ms2k_default_panel_config()
{
    PanelConfig c;
    c.btn_rows = 8;  c.btn_cols = 8;      // SW Matrix rács (T0..T7 × D0..D7)
    c.row_active_high = false;            // sor aktív alacsony (tipikus Korg)
    c.col_active_high = true;
    c.debounce_ms = 7;

    c.adc_bits = 10;  c.adc_vref_mv = 5000;
    c.num_pots = (int)ms2k_pot_names().size();

    c.led_rows = 8; c.led_cols = 8;       // LED mátrix 8×8 szkenneléssel
    c.led_row_active_high = true;
    c.led_col_active_high = true;
    return c;
}

// PanelIO feltöltése a mappingokkal
inline void ms2k_load_mappings(PanelIO& pio)
{
    pio.setPotMuxMap(ms2k_pot_to_mux_addr_simple());
    pio.setButtonPhysicalMap(ms2k_buttons_sw_matrix());
    pio.setLedWiring(true, true);
}

} // namespace MS2000
