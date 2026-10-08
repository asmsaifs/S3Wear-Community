// RTC drift calibration and system-clock discipline. See time_drift.h.
#include "time_drift.h"

#include <string.h>

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

// Round half away from zero.
static int64_t div_round(int64_t a, int64_t b)
{
    return (a < 0) == (b < 0) ? (a + b / 2) / b : (a - b / 2) / b;
}

static int8_t clamp_steps(int64_t s)
{
    return (int8_t)(s < TIME_DRIFT_STEPS_MIN ? TIME_DRIFT_STEPS_MIN : s > TIME_DRIFT_STEPS_MAX ? TIME_DRIFT_STEPS_MAX : s);
}

void time_drift_init(time_drift_t *d, int8_t steps)
{
    memset(d, 0, sizeof *d);
    d->steps = clamp_steps(steps);
}

void time_drift_reset_window(time_drift_t *d)
{
    d->anchor_ms = 0;
    d->err_ms = 0;
    d->rtc_bias_ms = 0;
}

static time_drift_result_t restart(time_drift_t *d, int64_t ref_ms, time_drift_result_t why, bool *rewrite)
{
    d->anchor_ms = ref_ms;
    d->err_ms = 0;
    d->rtc_bias_ms = 0;
    *rewrite = true;
    return why;
}

time_drift_result_t time_drift_on_sync(time_drift_t *d, int64_t ref_ms, int64_t rtc_s, bool rtc_valid,
                                       bool *rewrite_rtc)
{
    if (!rtc_valid || d->anchor_ms <= 0) {
        return restart(d, ref_ms, TIME_DRIFT_ANCHORED, rewrite_rtc);
    }
    const int64_t span_ms = ref_ms - d->anchor_ms;
    if (span_ms <= 0) {
        return restart(d, ref_ms, TIME_DRIFT_REJECTED, rewrite_rtc); // reference went backwards
    }
    // Error since the last rewrite (the RTC was then set exactly on a whole second).
    const int64_t err = rtc_s * 1000 + 500 - ref_ms;
    const int64_t total = d->err_ms + err;
    const int64_t limit = span_ms / 1000 * TIME_DRIFT_MAX_PPM / 1000 + TIME_DRIFT_QUANT_MS;
    if (abs64(err) > limit || abs64(total) > limit) {
        return restart(d, ref_ms, TIME_DRIFT_REJECTED, rewrite_rtc);
    }
    if (span_ms < (int64_t)TIME_DRIFT_MIN_SPAN_S * 1000) {
        *rewrite_rtc = abs64(err) >= TIME_DRIFT_REWRITE_MS;
        if (*rewrite_rtc) {
            d->err_ms = total;
            d->rtc_bias_ms = 0;
        } else {
            d->rtc_bias_ms = (int32_t)err;
        }
        return TIME_DRIFT_MEASURING;
    }
    // Rate of the RTC with the current steps applied; correct by the remaining part.
    const int64_t ppb = div_round(total * 1000000000LL, span_ms);
    if (abs64(ppb) >= TIME_DRIFT_DEADBAND_PPB) {
        d->steps = clamp_steps(d->steps + div_round(ppb, TIME_DRIFT_STEP_PPB));
    }
    d->last_ppb = (int32_t)ppb;
    return restart(d, ref_ms, TIME_DRIFT_ADJUSTED, rewrite_rtc);
}

int64_t time_discipline_correction(const time_drift_t *d, int64_t sys_ms, int64_t rtc_s)
{
    const int64_t err = sys_ms - (rtc_s * 1000 + 500 - d->rtc_bias_ms);
    return abs64(err) <= TIME_DISCIPLINE_TOL_MS ? 0 : -err;
}
