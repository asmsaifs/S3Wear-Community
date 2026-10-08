// RTC drift calibration and system-clock discipline (pure logic, no ESP-IDF: host-tested).
//
// Calibration (docs/03 F1): each accurate sync (phone, SNTP) compares the PCF85063 with
// the reference. The RTC reads whole seconds, so one reading is +/-0.5 s; to keep that
// noise from piling up, the RTC is rewritten (on a whole second) only when its error
// reaches TIME_DRIFT_REWRITE_MS or a window ends, and the errors at those rewrites are
// summed. After at least TIME_DRIFT_MIN_SPAN_S the rate (ppb, positive = RTC fast)
// becomes offset-register steps, unless it is inside the deadband. A positive step
// slows the RTC by TIME_DRIFT_STEP_PPB, so a fast RTC gets more steps (7-bit signed).
//
// Discipline: the ESP32-S3 has no 32 kHz crystal on this board, so system time runs on
// the RC slow clock in light sleep. svc_time compares it with the RTC now and then and
// steps it when they disagree by more than the RTC's resolution allows.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIME_DRIFT_MIN_SPAN_S   (48 * 3600) // window length: 0.5 s / 48 h = 3 ppm per reading
#define TIME_DRIFT_DEADBAND_PPB 6000        // ~0.5 s/day: below this keep the steps (spec: 2 s/day)
#define TIME_DRIFT_REWRITE_MS   1000        // rewrite the RTC mid-window once it is this far off
#define TIME_DRIFT_MAX_PPM      200         // larger: someone else set a clock, not drift
#define TIME_DRIFT_QUANT_MS     1500        // RTC read (1 s) + sync latency per measurement
#define TIME_DRIFT_STEP_PPB     4340        // PCF85063 normal mode (4.34 ppm, applied every 2 h)
#define TIME_DRIFT_STEPS_MIN    (-64)
#define TIME_DRIFT_STEPS_MAX    63
#define TIME_DISCIPLINE_TOL_MS  1000        // RTC reads whole seconds: +/-0.5 s plus margin

typedef struct {
    int64_t anchor_ms;   // reference time at the start of the window; 0 = no window
    int64_t err_ms;      // RTC error (RTC - reference) committed at rewrites since anchor_ms
    int32_t rtc_bias_ms; // RTC error at the last sync if it was not rewritten (else 0)
    int32_t last_ppb;    // last measured residual rate, for diagnostics
    int8_t steps;        // offset register value in use
} time_drift_t;

typedef enum {
    TIME_DRIFT_ANCHORED = 0, // window (re)started; nothing measured
    TIME_DRIFT_MEASURING,    // error noted, window still shorter than the minimum
    TIME_DRIFT_ADJUSTED,     // window complete: steps updated (unchanged inside the deadband), new window
    TIME_DRIFT_REJECTED,     // implausible error (clock set elsewhere): new window
} time_drift_result_t;

void time_drift_init(time_drift_t *d, int8_t steps);

/**
 * An accurate reference ref_ms (Unix ms) arrived; rtc_s is the RTC reading taken just
 * before it, rtc_valid false if the RTC lost power. *rewrite_rtc tells the caller to
 * rewrite the RTC on the next whole second (always, except mid-window while the RTC
 * is within TIME_DRIFT_REWRITE_MS). On ADJUSTED, write d->steps to the offset register.
 */
time_drift_result_t time_drift_on_sync(time_drift_t *d, int64_t ref_ms, int64_t rtc_s, bool rtc_valid,
                                       bool *rewrite_rtc);

/** The RTC was set from a source that is not a reference (manual, console): drop the window. */
void time_drift_reset_window(time_drift_t *d);

/**
 * Correction (ms) to add to the system time sys_ms given the RTC reading rtc_s, or 0
 * when they agree within TIME_DISCIPLINE_TOL_MS. The RTC ticks on whole seconds of
 * true time plus d->rtc_bias_ms, so the best estimate of true time is
 * rtc_s + 0.5 s - rtc_bias_ms.
 */
int64_t time_discipline_correction(const time_drift_t *d, int64_t sys_ms, int64_t rtc_s);

#ifdef __cplusplus
}
#endif
