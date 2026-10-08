// AXP2101 register encoding (pure logic, no ESP-IDF: host-tested).
// Register map: AXP2101 datasheet, cross-checked with XPowersLib (docs/vendor/README.md).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AXP2101_DCDC1 = 0,
    AXP2101_DCDC2,
    AXP2101_DCDC3,
    AXP2101_DCDC4,
    AXP2101_DCDC5,
    AXP2101_ALDO1,
    AXP2101_ALDO2,
    AXP2101_ALDO3,
    AXP2101_ALDO4,
    AXP2101_BLDO1,
    AXP2101_BLDO2,
    AXP2101_CPUSLDO,
    AXP2101_DLDO1,
    AXP2101_DLDO2,
    AXP2101_RAIL_COUNT,
} axp2101_rail_t;

typedef struct {
    const char *name;
    uint8_t en_reg;  // on/off register
    uint8_t en_bit;
    uint8_t vol_reg; // voltage register
    uint8_t vol_mask;
} axp2101_rail_info_t;

const axp2101_rail_info_t *axp2101_rail_info(axp2101_rail_t rail);

/** Voltage register code -> mV (0 if the code is out of the rail's range). */
uint16_t axp2101_rail_code_to_mv(axp2101_rail_t rail, uint8_t code);

/** mV -> code. false if mv is not exactly representable on that rail. */
bool axp2101_rail_mv_to_code(axp2101_rail_t rail, uint16_t mv, uint8_t *code);

/** Constant charge current (REG 0x62[4:0]): rounds down; 25 mA steps to 200 mA, then 100 mA to 1000 mA. */
uint8_t axp2101_ichg_code(uint16_t ma);
uint16_t axp2101_ichg_ma(uint8_t code);

/** Termination and precharge current (25 mA steps, 0..200 mA), rounds down. */
uint8_t axp2101_i25_code(uint16_t ma);
uint16_t axp2101_i25_ma(uint8_t code);

/** Charge target voltage (REG 0x64[2:0]). false if mv is not one of 4000/4100/4200/4350/4400. */
bool axp2101_vterm_code(uint16_t mv, uint8_t *code);
uint16_t axp2101_vterm_mv(uint8_t code);

/** 14-bit ADC result from a high/low register pair (high byte masked to hi_bits). */
uint16_t axp2101_adc14(uint8_t hi, uint8_t lo, uint8_t hi_bits);

/** Die temperature ADC (REG 0x3C/0x3D) -> tenths of °C: 22 + (7274 - raw) / 20. */
int16_t axp2101_die_temp_dc(uint16_t raw);

#ifdef __cplusplus
}
#endif
