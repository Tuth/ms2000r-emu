#include "hw_spec.h"

void init_hardware() {
    // Initialize LCD (HD44780)
    write_register(LCD_E, 1);
    delay_ms(100);
    write_register(LCD_E, 0);
    
    // Initialize DSP link (8-bit UART)
    write_register(DSP_LINK_TXD, 0x00);
    write_register(DSP_LINK_RXD, 0x00);
    write_register(DSP_LINK_SCK, 0x00);
    
    // Initialize I2S audio (placeholder)
}