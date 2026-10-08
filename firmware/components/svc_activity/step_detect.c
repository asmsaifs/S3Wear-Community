// Software step detector (step_detect.h). Pure C, host-tested (test_step_detect).
#include "step_detect.h"

#include <math.h>
#include <string.h>

#define TAU_LP_MS   40.0f   // low-pass of |a|: ~4 Hz, keeps the step and drops the jitter
#define TAU_BASE_MS 1000.0f // baseline of |a| (gravity plus slow posture changes)
#define GAP_MS      1000    // longer between samples: restart the filters
#define AMP_KEEP    0.75f   // mean peak height: weight of the old value

void step_detect_init(step_detect_t *sd)
{
    memset(sd, 0, sizeof *sd);
}

static void run_end(step_detect_t *sd)
{
    sd->have_step = false;
    sd->run = 0;
    sd->walking = false;
}

// A peak at t with height h. Returns the steps it counts.
static uint32_t candidate(step_detect_t *sd, uint32_t t, float h)
{
    const uint32_t dt = t - sd->last_step_ms;
    if (sd->have_step && dt < STEP_MIN_MS) {
        return 0; // a second bump within one step
    }
    if (!sd->have_step || dt > STEP_MAX_MS) {
        // First candidate of a new run: the threshold starts from this peak.
        run_end(sd);
        sd->have_step = true;
        sd->last_step_ms = t;
        sd->run = 1;
        sd->amp = h;
        return 0;
    }
    sd->amp = sd->amp * AMP_KEEP + h * (1.0f - AMP_KEEP);
    const float iv = (float)dt;
    if (sd->run >= 2 && !sd->walking && (iv < sd->interval * 0.5f || iv > sd->interval * 1.6f)) {
        // Not steady yet: this candidate starts the next attempt at a run.
        sd->run = 1;
        sd->last_step_ms = t;
        return 0;
    }
    sd->interval = sd->run == 1 ? iv : sd->interval * 0.7f + iv * 0.3f;
    sd->last_step_ms = t;
    if (sd->run < UINT16_MAX) {
        sd->run++;
    }
    if (sd->walking) {
        return 1;
    }
    if (sd->run >= STEP_RUN_MIN) {
        sd->walking = true;
        return sd->run;
    }
    return 0;
}

uint32_t step_detect_sample(step_detect_t *sd, uint32_t t_ms, int32_t x, int32_t y, int32_t z)
{
    const float m = sqrtf((float)x * (float)x + (float)y * (float)y + (float)z * (float)z);
    const uint32_t dt = t_ms - sd->last_ms;
    if (!sd->init || dt > GAP_MS) {
        sd->init = true;
        sd->lp = m;
        sd->base = m;
        sd->above = false;
        sd->last_ms = t_ms;
        return 0;
    }
    sd->last_ms = t_ms;
    if (dt == 0) {
        return 0;
    }
    const float fdt = (float)dt;
    sd->lp += (m - sd->lp) * fdt / (TAU_LP_MS + fdt);
    sd->base += (m - sd->base) * fdt / (TAU_BASE_MS + fdt);
    const float d = sd->lp - sd->base;

    if (sd->have_step && t_ms - sd->last_step_ms > STEP_MAX_MS) {
        run_end(sd); // stopped walking
    }
    float th = sd->amp * STEP_TH_RATIO;
    if (th < STEP_TH_MIN_MG) {
        th = STEP_TH_MIN_MG;
    }
    uint32_t steps = 0;
    if (!sd->above) {
        if (d > th) {
            sd->above = true;
            sd->peak = d;
            sd->peak_ms = t_ms;
        }
    } else if (d > sd->peak) {
        sd->peak = d;
        sd->peak_ms = t_ms;
    } else if (d < 0.0f) {
        sd->above = false;
        steps = candidate(sd, sd->peak_ms, sd->peak);
    }
    sd->total += steps;
    return steps;
}

uint16_t step_detect_cadence(const step_detect_t *sd)
{
    if (!sd->walking || sd->interval < 1.0f) {
        return 0;
    }
    return (uint16_t)lroundf(60000.0f / sd->interval);
}
