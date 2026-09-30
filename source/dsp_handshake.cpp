#include "hw_spec.h"

void check_dsp_handshake() {
    // Send test command to DSP (0x55 = acknowledge)
    digitalWrite(DSP_LINK_TXD, 0x55);
    delay_ms(1);

    // Verify response from DSP
    if (digitalRead(DSP_LINK_RXD) == HIGH) {
        Logger::instance().log("DSP handshake successful");
    }
}