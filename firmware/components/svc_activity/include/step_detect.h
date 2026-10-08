// Software step detector for the wrist (P5-01, docs/03 F12). Pure C: also built by
// firmware/host_test (test_step_detect runs it over recorded accelerometer CSVs).
//
// Works on |a| so the watch's orientation does not matter, at any sample rate (the time
// of each sample is given; svc_sensors' FIFO runs at 21 or 62.5 Hz):
//   1. |a| low-passed (~4 Hz) minus a slow baseline (~1 s): the motion around gravity.
//   2. A peak = the signal rose above the threshold and came back below zero; the
//      threshold adapts to the recent peaks (STEP_TH_RATIO of their mean, at least
//      STEP_TH_MIN_MG).
//   3. Peaks 250 ms..2 s apart are step candidates. Steps only count inside a rhythmic run:
//      STEP_RUN_MIN candidates at a steady interval (each within ×0.5..×1.6 of the mean)
//      must come first, and then all of them count at once. A pause longer than 2 s ends
//      the run. Single arm movements, typing and taps never reach a run.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STEP_TH_MIN_MG    50   // smallest peak that can be a step (desk noise is < 20 mg)
#define STEP_TH_RATIO     0.4f // threshold = this × mean recent peak height
#define STEP_MIN_MS       250  // 240 spm: faster is a bounce within one step
#define STEP_MAX_MS       2000 // 30 spm: slower ends the run
#define STEP_RUN_MIN      8    // candidates before a run counts

typedef struct {
    bool init;
    uint32_t last_ms;      // last sample
    float lp;              // low-passed |a|, mg
    float base;            // slow baseline of |a|, mg
    bool above;            // looking for the top of a peak
    float peak;            // its height so far, mg
    uint32_t peak_ms;
    float amp;             // mean recent peak height, mg (0 = none yet)
    bool have_step;        // last_step_ms is valid
    uint32_t last_step_ms; // last candidate in the run
    float interval;        // mean interval in the run, ms
    uint16_t run;          // candidates in the run
    bool walking;          // the run reached STEP_RUN_MIN: every new candidate counts
    uint32_t total;        // steps counted since init
} step_detect_t;

void step_detect_init(step_detect_t *sd);

/**
 * One accelerometer sample (mg, any frame) at t_ms (monotonic, wraps fine). Returns the
 * steps counted by it: 0, 1, or a whole run's worth when a run is confirmed.
 * A gap of more than 1 s between samples restarts the filters.
 */
uint32_t step_detect_sample(step_detect_t *sd, uint32_t t_ms, int32_t x, int32_t y, int32_t z);

/** Steps per minute of the current run, 0 when not walking. */
uint16_t step_detect_cadence(const step_detect_t *sd);

#ifdef __cplusplus
}
#endif
