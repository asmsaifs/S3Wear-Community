// FT3168 capacitive touch controller (I2C). Coordinate reads go through
// esp_lcd_touch_ft5x06 (register-compatible); this driver adds power modes and ID.
// Pin-agnostic: the caller passes GPIOs and the shared bus handle.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ft3168_s *ft3168_handle_t;

typedef enum {
    FT3168_PMODE_ACTIVE = 0,
    FT3168_PMODE_MONITOR = 1,   // low-rate scan; a touch raises INT and returns to active
    FT3168_PMODE_STANDBY = 2,
    FT3168_PMODE_HIBERNATE = 3, // lowest power; needs a hardware reset to wake
} ft3168_pmode_t;

/** INT falling edge. Runs in ISR context: only give a semaphore / notify a task. */
typedef void (*ft3168_isr_cb_t)(void *ctx);

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t addr;
    int rst_gpio;
    int int_gpio;
    uint16_t x_max;
    uint16_t y_max;
    uint32_t scl_hz;
    ft3168_isr_cb_t isr_cb;
    void *isr_ctx;
} ft3168_config_t;

/** Reset (if rst_gpio >= 0), init and register the INT handler. */
esp_err_t ft3168_new(const ft3168_config_t *cfg, ft3168_handle_t *out);

/** Blocking I2C read of the first touch point. Returns true while touched. */
bool ft3168_read(ft3168_handle_t h, uint16_t *x, uint16_t *y);

/** Points the FT3168 tracks (registers 0x03.., 6 bytes each). */
#define FT3168_MAX_POINTS 2

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t weight; // TOUCHn_WEIGHT
    uint8_t area;   // TOUCHn_MISC[7:4]
} ft3168_point_t;

typedef struct {
    uint8_t count; // TD_STATUS[3:0] as reported: may exceed FT3168_MAX_POINTS (large contact)
    uint8_t valid; // entries filled in pt[] (min(count, FT3168_MAX_POINTS), 0 if count > 5)
    ft3168_point_t pt[FT3168_MAX_POINTS];
} ft3168_contact_t;

/** Blocking I2C read of the point count and all tracked points (one transfer). */
esp_err_t ft3168_read_contact(ft3168_handle_t h, ft3168_contact_t *out);

esp_err_t ft3168_set_power_mode(ft3168_handle_t h, ft3168_pmode_t mode);
esp_err_t ft3168_get_power_mode(ft3168_handle_t h, ft3168_pmode_t *mode);

/** Chip ID register 0xA0: 0x03 = FT3168. Also returns firmware version (0xA6). */
esp_err_t ft3168_read_id(ft3168_handle_t h, uint8_t *chip_id, uint8_t *fw_ver);

/** Hardware reset + re-init (the only way out of hibernate). */
esp_err_t ft3168_reset(ft3168_handle_t h);

#ifdef __cplusplus
}
#endif
