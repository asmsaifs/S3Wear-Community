// WATCH-ONLY timing (docs/02-firmware-architecture.md §7). Pure logic, no ESP-IDF:
// built by firmware/host_test.
//
// In WATCH-ONLY the ESP32-S3 deep-sleeps with the panel left on, showing the time.
// The RTC timer wakes it once a minute (a short boot redraws the time and sleeps
// again) and before the next alarm. The timer runs on the RC slow clock, a few %
// off, so a tick aims WATCH_ONLY_TICK_LATE_MS after the minute starts, and a tick
// that arrives up to WATCH_ONLY_EARLY_MS before it already shows the new minute.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_ONLY_TICK_LATE_MS     1500
#define WATCH_ONLY_EARLY_MS         3000
/** A wake this close before the alarm (or later) boots fully and waits for it. */
#define WATCH_ONLY_ALARM_BOOT_S     70
/** The alarm wake comes early by 1/WATCH_ONLY_ALARM_LEAD_DIV of the wait, at least
 *  WATCH_ONLY_ALARM_LEAD_MIN_S (RC clock error). */
#define WATCH_ONLY_ALARM_LEAD_MIN_S 5
#define WATCH_ONLY_ALARM_LEAD_DIV   50
/** At or below this battery % the panel goes off too (no minute ticks). */
#define WATCH_ONLY_DARK_PCT         1

typedef enum {
    WATCH_ONLY_WAKE_TICK,  // redraw the time and sleep again
    WATCH_ONLY_WAKE_ALARM, // boot fully: the alarm rings soon
} watch_only_wake_t;

/** The minute (UTC ms / 60000) a tick at now_ms shows. */
int64_t watch_only_minute(int64_t now_ms);

/**
 * Deep-sleep time in µs from now_ms (UTC): the next minute tick if ticks, and the
 * alarm wake if alarm_s > 0 (UTC s), whichever is first. 0 = no timer (PWR only).
 */
uint64_t watch_only_sleep_us(int64_t now_ms, int64_t alarm_s, bool ticks);

/** What a timer wake at now_s is for. */
watch_only_wake_t watch_only_wake_kind(int64_t now_s, int64_t alarm_s);

#ifdef __cplusplus
}
#endif
