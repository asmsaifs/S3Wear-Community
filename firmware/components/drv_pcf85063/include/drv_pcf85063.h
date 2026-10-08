// PCF85063A real-time clock (I2C). Holds UTC; time zone handling is the time
// service's job. Pin-agnostic: caller passes the bus and the INT GPIO.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "pcf85063_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcf85063_s *pcf85063_handle_t;

/** Alarm INT (falling edge, open-drain). ISR context: only notify/give. */
typedef void (*pcf85063_isr_cb_t)(void *ctx);

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t addr;
    uint32_t scl_hz;
    int int_gpio;            // -1 = not used
    pcf85063_isr_cb_t isr_cb;
    void *isr_ctx;
} pcf85063_config_t;

/** Probe, make sure the clock runs in 24 h mode and disable CLKOUT (saves power). */
esp_err_t pcf85063_new(const pcf85063_config_t *cfg, pcf85063_handle_t *out);

/** Read UTC. *osc_stopped = OS flag (time invalid since last set). */
esp_err_t pcf85063_get_time(pcf85063_handle_t h, struct tm *utc, bool *osc_stopped);

/** Write UTC (2000..2099) and clear the OS flag. */
esp_err_t pcf85063_set_time(pcf85063_handle_t h, const struct tm *utc);

/** Alarm at an exact UTC second (day-of-month/hour/minute/second match); enables AIE + INT. */
esp_err_t pcf85063_set_alarm(pcf85063_handle_t h, const struct tm *utc);

/** Disable the alarm interrupt and clear AF. */
esp_err_t pcf85063_disable_alarm(pcf85063_handle_t h);

/** Read AF (alarm flag); if set, clear it so INT is released. */
esp_err_t pcf85063_check_alarm(pcf85063_handle_t h, bool *fired);

esp_err_t pcf85063_set_offset(pcf85063_handle_t h, int8_t steps, bool coarse);
esp_err_t pcf85063_get_offset(pcf85063_handle_t h, int8_t *steps, bool *coarse);

#ifdef __cplusplus
}
#endif
