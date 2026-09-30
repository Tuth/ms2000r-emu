#include "h8s2350_v2_1.h"

namespace MS2000 {

Peripherals::Peripherals(H8SBus& bus, PanelIF& p) : panel_(p){
    map_ports(bus);
    map_adc(bus);
    map_sci(bus);
}

// --------- PORTS (from ms2k_fw_ports_map.h) ---------
void Peripherals::map_ports(H8SBus& bus){
    // Column read
    bus.map({ MS2K_PORT_COL_ADDR, MS2K_PORT_COL_ADDR,
        [this](uint32_t){ return panel_.readColPort(); },
        nullptr, "PORT COL (read)" });

    // Row drive
    bus.map({ MS2K_PORT_ROW_ADDR, MS2K_PORT_ROW_ADDR,
        [this](uint32_t){ return last_row; },
        [this](uint32_t, uint8_t v){ last_row=v; panel_.writeRowPort(v); },
        "PORT ROW (write)" });

    // LED row select
    bus.map({ MS2K_LED_ROW_ADDR, MS2K_LED_ROW_ADDR,
        [this](uint32_t){ return last_led_row; },
        [this](uint32_t, uint8_t v){ last_led_row=v; panel_.writeLedRowSel(v); },
        "LED ROW (write)" });

    // LED columns
    bus.map({ MS2K_LED_COL_ADDR, MS2K_LED_COL_ADDR,
        [this](uint32_t){ return last_led_cols; },
        [this](uint32_t, uint8_t v){ last_led_cols=v; panel_.writeLedCols(v); },
        "LED COLS (write)" });
}

// --------- ADC (ADCSR/ADCR + ADDR[A..D]H/L) ---------
void Peripherals::map_adc(H8SBus& bus){
    const uint32_t lo = MS2K_ADDA_H_ADDR;
    const uint32_t hi = MS2K_ADCR_ADDR;
    bus.map({ lo, hi,
        // r8
        [this](uint32_t a){
            uint16_t val = panel_.adcRead();
            switch(a){
                case MS2K_ADDA_H_ADDR: case MS2K_ADDB_H_ADDR:
                case MS2K_ADDC_H_ADDR: case MS2K_ADDD_H_ADDR:
                    return (uint8_t)((val >> 8) & 0x03);
                case MS2K_ADDA_L_ADDR: case MS2K_ADDB_L_ADDR:
                case MS2K_ADDC_L_ADDR: case MS2K_ADDD_L_ADDR:
                    return (uint8_t)(val & 0xFF);
                case MS2K_ADCSR_ADDR: return ADCSR;
                case MS2K_ADCR_ADDR:  return ADCR;
                default: return (uint8_t)0xFF;
            }
        },
        // w8
        [this](uint32_t a, uint8_t v){
            if(a==MS2K_ADCSR_ADDR){
                ADCSR = (uint8_t)(v & 0x7F);
                // CH2..0 (bits2..0) + CH3 (bit3) -> mux 0..7
                uint8_t ch = ADCSR & 0x07;
                uint8_t grp = (ADCSR >> 3) & 0x01;
                panel_.adcSetMux( (grp<<2) | (ch & 0x03) );
            } else if (a==MS2K_ADCR_ADDR){
                ADCR = v;
            }
        },
        "ADC10"
    });
}

// --------- SCI0/SCI1 ---------
void Peripherals::map_sci(H8SBus& bus){
    // SCI0
    bus.map({ MS2K_SCI0_SMR, MS2K_SCI0_SCMR,
        [this](uint32_t a){
            switch(a){
                case MS2K_SCI0_SMR:  return SMR0;
                case MS2K_SCI0_BRR:  return BRR0;
                case MS2K_SCI0_SCR:  return SCR0;
                case MS2K_SCI0_TDR:  return TDR0;
                case MS2K_SCI0_SSR:  return SSR0;
                case MS2K_SCI0_RDR:  return RDR0;
                case MS2K_SCI0_SCMR: return SCMR0;
            } return (uint8_t)0xFF;
        },
        [this](uint32_t a, uint8_t v){
            switch(a){
                case MS2K_SCI0_SMR:  SMR0=v; break;
                case MS2K_SCI0_BRR:  BRR0=v; break;
                case MS2K_SCI0_SCR:  SCR0=v; break;
                case MS2K_SCI0_TDR:  TDR0=v; SSR0|=0x80; break; // TDRE=1
                case MS2K_SCI0_SSR:  SSR0 &= (uint8_t)~(v & 0xF8); break;
                case MS2K_SCI0_RDR:  RDR0=v; SSR0|=0x40; break; // RDRF=1
                case MS2K_SCI0_SCMR: SCMR0=v; break;
            }
        },
        "SCI0"
    });

    // SCI1
    bus.map({ MS2K_SCI1_SMR, MS2K_SCI1_SCMR,
        [this](uint32_t a){
            switch(a){
                case MS2K_SCI1_SMR:  return SMR1;
                case MS2K_SCI1_BRR:  return BRR1;
                case MS2K_SCI1_SCR:  return SCR1;
                case MS2K_SCI1_TDR:  return TDR1;
                case MS2K_SCI1_SSR:  return SSR1;
                case MS2K_SCI1_RDR:  return RDR1;
                case MS2K_SCI1_SCMR: return SCMR1;
            } return (uint8_t)0xFF;
        },
        [this](uint32_t a, uint8_t v){
            switch(a){
                case MS2K_SCI1_SMR:  SMR1=v; break;
                case MS2K_SCI1_BRR:  BRR1=v; break;
                case MS2K_SCI1_SCR:  SCR1=v; break;
                case MS2K_SCI1_TDR:  TDR1=v; SSR1|=0x80; break;
                case MS2K_SCI1_SSR:  SSR1 &= (uint8_t)~(v & 0xF8); break;
                case MS2K_SCI1_RDR:  RDR1=v; SSR1|=0x40; break;
                case MS2K_SCI1_SCMR: SCMR1=v; break;
            }
        },
        "SCI1"
    });
}

} // namespace MS2000
