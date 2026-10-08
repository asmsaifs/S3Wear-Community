// AXP2101 PMU: rails, charger, fuel gauge, ADCs, power key, IRQ status, shutdown.
// I2C only and pin-agnostic. On this board the PMU IRQ pin is not wired to the SoC,
// so the caller decides when to read axp2101_irq_read_clear() (docs/01-hardware.md §2).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "axp2101_codec.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct axp2101_s *axp2101_handle_t;

// IRQ bits: INTSTS1 -> bits 0..7, INTSTS2 -> 8..15, INTSTS3 -> 16..23 (same for INTEN).
#define AXP2101_IRQ_SOC_WARN1    (1u << 6)
#define AXP2101_IRQ_SOC_WARN2    (1u << 7)
#define AXP2101_IRQ_PKEY_RISE    (1u << 8)
#define AXP2101_IRQ_PKEY_FALL    (1u << 9)
#define AXP2101_IRQ_PKEY_LONG    (1u << 10)
#define AXP2101_IRQ_PKEY_SHORT   (1u << 11)
#define AXP2101_IRQ_BAT_REMOVE   (1u << 12)
#define AXP2101_IRQ_BAT_INSERT   (1u << 13)
#define AXP2101_IRQ_VBUS_REMOVE  (1u << 14)
#define AXP2101_IRQ_VBUS_INSERT  (1u << 15)
#define AXP2101_IRQ_DIE_OVERTEMP (1u << 18)
#define AXP2101_IRQ_CHG_START    (1u << 19)
#define AXP2101_IRQ_CHG_DONE     (1u << 20)

typedef enum {
    AXP2101_CHG_TRICKLE = 0,
    AXP2101_CHG_PRECHARGE,
    AXP2101_CHG_CC,
    AXP2101_CHG_CV,
    AXP2101_CHG_DONE,
    AXP2101_CHG_NOT_CHARGING,
} axp2101_chg_state_t;

typedef struct {
    bool vbus_good;
    bool battery_present;
    bool charging;            // battery current direction = charge
    bool discharging;
    axp2101_chg_state_t chg_state;
    int8_t battery_pct;       // fuel gauge, -1 if no battery
    uint16_t vbat_mv;         // 0 if no battery
    uint16_t vbus_mv;         // 0 if no VBUS
    uint16_t vsys_mv;
    int16_t die_temp_dc;      // tenths of °C
} axp2101_status_t;

typedef struct {
    uint16_t cc_ma;        // constant current (<= 0.5 C for the 400 mAh cell: 200 mA)
    uint16_t term_ma;
    uint16_t precharge_ma;
    uint16_t cv_mv;        // 4000/4100/4200/4350/4400
} axp2101_charger_cfg_t;

typedef enum {
    AXP2101_PKEY_OFF_4S = 0,
    AXP2101_PKEY_OFF_6S,
    AXP2101_PKEY_OFF_8S,
    AXP2101_PKEY_OFF_10S,
} axp2101_pkey_off_t;

esp_err_t axp2101_new(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_hz, axp2101_handle_t *out);

esp_err_t axp2101_rail_get(axp2101_handle_t h, axp2101_rail_t rail, bool *on, uint16_t *mv);
/** mv = 0 keeps the voltage. Refuses to switch DCDC1 (SoC supply) off. */
esp_err_t axp2101_rail_set(axp2101_handle_t h, axp2101_rail_t rail, bool on, uint16_t mv);

esp_err_t axp2101_charger_config(axp2101_handle_t h, const axp2101_charger_cfg_t *cfg);
esp_err_t axp2101_charger_get(axp2101_handle_t h, axp2101_charger_cfg_t *cfg);

/** Enable battery/VBUS/VSYS/die-temp ADCs and the fuel gauge; disable the TS (NTC) input. */
esp_err_t axp2101_adc_setup(axp2101_handle_t h, bool battery_ntc);

esp_err_t axp2101_read_status(axp2101_handle_t h, axp2101_status_t *st);

/** Long-press time for the hardware power-off, and enable it (key held -> PMU off). */
esp_err_t axp2101_pkey_config(axp2101_handle_t h, axp2101_pkey_off_t off_time);
esp_err_t axp2101_pkey_get(axp2101_handle_t h, axp2101_pkey_off_t *off_time);

/** Replace the IRQ enable mask (AXP2101_IRQ_* bits) and clear pending status. */
esp_err_t axp2101_irq_set_mask(axp2101_handle_t h, uint32_t mask);
/** Read pending IRQ status and clear exactly those bits. */
esp_err_t axp2101_irq_read_clear(axp2101_handle_t h, uint32_t *status);

/** REG 0x20 (power-on source) and 0x21 (power-off source) bit fields. */
esp_err_t axp2101_power_sources(axp2101_handle_t h, uint8_t *on_src, uint8_t *off_src);

/** Software power-off (REG 0x10 bit 0). Everything except the RTC LDO goes down. */
esp_err_t axp2101_shutdown(axp2101_handle_t h);

#ifdef __cplusplus
}
#endif
