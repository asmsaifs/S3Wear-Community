#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RTC time as UTC seconds. *valid is false when the clock lost power (oscillator
 * stopped): the value is then meaningless and the UI shows "time unknown".
 */
esp_err_t hal_rtc_get(time_t *utc, bool *valid);

/** Set the RTC (also clears the lost-power state). */
esp_err_t hal_rtc_set(time_t utc);

/**
 * Crystal trim in steps of HAL_RTC_OFFSET_STEP_PPB; a positive value slows the clock
 * (PCF85063 offset register, normal mode). Lost when the RTC loses power.
 */
#define HAL_RTC_OFFSET_STEP_PPB 4340
esp_err_t hal_rtc_set_offset(int8_t steps);

/** Fire the alarm callback at utc (minute resolution on the watch). One alarm at a time. */
esp_err_t hal_rtc_set_alarm(time_t utc);
esp_err_t hal_rtc_cancel_alarm(void);

/** Short and non-blocking: runs in a timer/driver context. */
typedef void (*hal_rtc_alarm_cb_t)(void *ctx);
void hal_rtc_set_alarm_cb(hal_rtc_alarm_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
