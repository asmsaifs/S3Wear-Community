// Today's activity totals (P5-01, docs/03 F12). Pure C: also built by firmware/host_test
// (test_activity_day).
//
// Steps go into per-minute buckets of the local day; distance, active kcal and active
// minutes follow each minute's step count (its cadence):
//   - stride = height × 0.415 (male) / 0.413 (female) / 0.414 (not set); × 1.3 when the
//     minute's cadence is ≥ ACT_RUN_SPM (running);
//   - active kcal = (MET − 1) × weight × minutes, MET from the cadence (walking 2.8 at
//     60 spm .. 5.0 at 140 spm, running 8.3 / 9.8); a minute under 60 steps is prorated
//     at MET 2.0;
//   - an active minute has ≥ ACT_ACTIVE_SPM steps;
//   - total kcal = active kcal + BMR (Mifflin-St Jeor) prorated over the day so far.
// A step on a later local day ends the day (its summary is returned) and starts a new one.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACT_MINUTES    1440
#define ACT_ACTIVE_SPM 60  // steps in a minute for it to be an active minute
#define ACT_RUN_SPM    140 // cadence from which the stride is a running one

typedef enum { ACT_SEX_UNSET = 0, ACT_SEX_MALE = 1, ACT_SEX_FEMALE = 2 } act_sex_t;

typedef struct {
    uint16_t height_cm;
    uint16_t weight_kg;
    uint8_t sex;         // act_sex_t
    uint16_t birth_year; // for the BMR
    uint32_t step_goal;
    uint16_t active_goal_min;
} activity_profile_t;

typedef struct {
    uint32_t day;           // local date as yyyymmdd; 0 = not known yet (clock not set)
    uint32_t steps;
    uint32_t distance_cm;
    uint32_t active_mkcal;  // active kcal × 1000
    uint16_t active_min;
    bool step_goal_hit;
    bool active_goal_hit;
} activity_totals_t;

typedef struct {
    activity_totals_t t;
    uint16_t minute_steps[ACT_MINUTES]; // per local minute of the day
} activity_day_t;

typedef struct {
    uint32_t day;
    uint32_t steps;
    uint32_t distance_m;
    uint32_t kcal;        // active + BMR so far
    uint32_t active_kcal;
    uint16_t active_min;
    uint32_t step_goal;
    uint16_t active_goal_min;
} activity_summary_t;

// activity_day_add() results (bits)
#define ACT_DAY_ENDED     (1u << 0) // *ended holds the summary of the day that ended
#define ACT_STEP_GOAL     (1u << 1) // the step goal was reached just now
#define ACT_ACTIVE_GOAL   (1u << 2) // the active minutes goal was reached just now

void activity_day_init(activity_day_t *d, uint32_t day);

/**
 * steps more at local `day` (yyyymmdd, 0 = unknown) and `minute` (0..1439). steps may be 0
 * (only checks for a new day). A later day ends the current one; an earlier day (the clock
 * went back) or 0 keeps counting into the current one, and the first known day adopts the
 * counts made before it. Returns ACT_* bits.
 */
unsigned activity_day_add(activity_day_t *d, const activity_profile_t *p, uint32_t day, uint16_t minute,
                          uint32_t steps, activity_summary_t *ended);

/** Totals at local `minute` of the day (for the prorated BMR; ACT_MINUTES = whole day). */
void activity_day_summary(const activity_day_t *d, const activity_profile_t *p, uint16_t minute,
                          activity_summary_t *out);

/** Restore saved totals for the same day (the minute buckets start empty). */
void activity_day_restore(activity_day_t *d, const activity_totals_t *t);

uint32_t activity_stride_cm(const activity_profile_t *p, uint32_t spm);
/** Basal metabolic rate, kcal per day; age from `year`. */
uint32_t activity_bmr_kcal(const activity_profile_t *p, uint16_t year);
/** Active kcal × 1000 of one minute with spm steps. */
uint32_t activity_minute_mkcal(const activity_profile_t *p, uint32_t spm);

#ifdef __cplusplus
}
#endif
