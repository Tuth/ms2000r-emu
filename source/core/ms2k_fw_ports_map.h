#pragma once
//
// MS2000 firmware-driven I/O address map (extracted from x811v107.sys)
//
// If később pontosítod a címeket a FW-ből, itt elég átírni a konstansokat.
//

#define MS2K_PORT_COL_ADDR      0x00FF50  // PORT1 (panel column read)
#define MS2K_PORT_ROW_ADDR      0x00FF51  // PORT2 (panel row drive)
#define MS2K_LED_ROW_ADDR       0x00FF53  // PORT4 (LED row select)
#define MS2K_LED_COL_ADDR       0x00FF54  // PORT5 (LED columns mask)

// ADC10 registers
#define MS2K_ADDA_H_ADDR        0x00FF90
#define MS2K_ADDA_L_ADDR        0x00FF91
#define MS2K_ADDB_H_ADDR        0x00FF92
#define MS2K_ADDB_L_ADDR        0x00FF93
#define MS2K_ADDC_H_ADDR        0x00FF94
#define MS2K_ADDC_L_ADDR        0x00FF95
#define MS2K_ADDD_H_ADDR        0x00FF96
#define MS2K_ADDD_L_ADDR        0x00FF97
#define MS2K_ADCSR_ADDR         0x00FF98
#define MS2K_ADCR_ADDR          0x00FF99

// SCI0
#define MS2K_SCI0_SMR           0x00FF78
#define MS2K_SCI0_BRR           0x00FF79
#define MS2K_SCI0_SCR           0x00FF7A
#define MS2K_SCI0_TDR           0x00FF7B
#define MS2K_SCI0_SSR           0x00FF7C
#define MS2K_SCI0_RDR           0x00FF7D
#define MS2K_SCI0_SCMR          0x00FF7E

// SCI1
#define MS2K_SCI1_SMR           0x00FF80
#define MS2K_SCI1_BRR           0x00FF81
#define MS2K_SCI1_SCR           0x00FF82
#define MS2K_SCI1_TDR           0x00FF83
#define MS2K_SCI1_SSR           0x00FF84
#define MS2K_SCI1_RDR           0x00FF85
#define MS2K_SCI1_SCMR          0x00FF86
