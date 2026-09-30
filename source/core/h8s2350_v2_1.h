#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include "ms2k_fw_ports_map.h"

namespace MS2000 {

// ------------------------------
// Simple 8-bit bus mapper
// ------------------------------
class H8SBus {
public:
    using Read8  = std::function<uint8_t(uint32_t)>;
    using Write8 = std::function<void(uint32_t,uint8_t)>;
    struct Region { uint32_t start, end; Read8 r8; Write8 w8; const char* name; };
    void map(const Region& r){ regs_.push_back(r); }
    uint8_t  read8 (uint32_t a){ for (auto&r:regs_) if(a>=r.start && a<=r.end) return r.r8? r.r8(a):0xFF; return 0xFF; }
    void     write8(uint32_t a, uint8_t v){ for (auto&r:regs_) if(a>=r.start && a<=r.end){ if(r.w8) r.w8(a,v); return; } }
private: std::vector<Region> regs_;
};

// ------------------------------
// Panel IF expected by glue
// ------------------------------
struct PanelIF {
    virtual ~PanelIF() = default;
    virtual uint8_t  readColPort() = 0;
    virtual void     writeRowPort(uint8_t v) = 0;
    virtual void     writeLedRowSel(uint8_t v) = 0;
    virtual void     writeLedCols(uint8_t v) = 0;
    virtual void     adcSetMux(uint8_t mux) = 0;
    virtual uint16_t adcRead() = 0;
};

// ------------------------------
// Glue peripherals mapped to FW addresses
// ------------------------------
class Peripherals {
public:
    explicit Peripherals(H8SBus& bus, PanelIF& p);

    // Optional: last latched values for debug
    uint8_t last_row{0}, last_led_row{0}, last_led_cols{0};

private:
    PanelIF& panel_;
    // SCI0/1 mirrors
    uint8_t SMR0{0}, BRR0{0xFF}, SCR0{0}, SSR0{0x84}, TDR0{0xFF}, RDR0{0x00}, SCMR0{0xF2};
    uint8_t SMR1{0}, BRR1{0xFF}, SCR1{0}, SSR1{0x84}, TDR1{0xFF}, RDR1{0x00}, SCMR1{0xF2};
    // ADC
    uint8_t ADCSR{0}, ADCR{0x3F};
    void map_ports(H8SBus&);
    void map_adc(H8SBus&);
    void map_sci(H8SBus&);
};

} // namespace MS2000
