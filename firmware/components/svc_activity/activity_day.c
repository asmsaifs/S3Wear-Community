// Today's activity totals (activity_day.h). Pure C, host-tested (test_activity_day).
#include "activity_day.h"

#include <string.h>

void activity_day_init(activity_day_t *d, uint32_t day)
{
    memset(d, 0, sizeof *d);
    d->t.day = day;
}

void activity_day_restore(activity_day_t *d, const activity_totals_t *t)
{
    memset(d, 0, sizeof *d);
    d->t = *t;
}

uint32_t activity_stride_cm(const activity_profile_t *p, uint32_t spm)
{
    // Per mille of the height (common pedometer estimate; the phone can calibrate later).
    const uint32_t k = p->sex == ACT_SEX_MALE ? 415 : p->sex == ACT_SEX_FEMALE ? 413 : 414;
    uint32_t stride = (uint32_t)p->height_cm * k / 1000;
    if (spm >= ACT_RUN_SPM) {
        stride = stride * 13 / 10;
    }
    return stride;
}

uint32_t activity_bmr_kcal(const activity_profile_t *p, uint16_t year)
{
    // Mifflin-St Jeor: 10 W + 6.25 H - 5 A + s (s = +5 male, -161 female; the mean if unset).
    int32_t age = year > p->birth_year ? (int32_t)year - p->birth_year : 0;
    if (age > 120) {
        age = 120;
    }
    const int32_t s = p->sex == ACT_SEX_MALE ? 5 : p->sex == ACT_SEX_FEMALE ? -161 : -78;
    const int32_t bmr = (1000 * p->weight_kg + 625 * p->height_cm - 500 * age + 100 * s) / 100;
    return bmr > 0 ? (uint32_t)bmr : 0;
}

// MET × 10 for a minute's cadence (Compendium of Physical Activities: walking 2.0 mph 2.8
// .. 4.0 mph 5.0, running 5 mph 8.3, 6 mph 9.8).
static uint32_t met10(uint32_t spm)
{
    if (spm < 90) {
        return 28;
    }
    if (spm < 110) {
        return 35;
    }
    if (spm < 125) {
        return 43;
    }
    if (spm < ACT_RUN_SPM) {
        return 50;
    }
    return spm < 160 ? 83 : 98;
}

uint32_t activity_minute_mkcal(const activity_profile_t *p, uint32_t spm)
{
    if (spm == 0) {
        return 0;
    }
    // Net kcal/min = (MET - 1) × kg / 60; × 1000 for mkcal, MET in tenths.
    if (spm < ACT_ACTIVE_SPM) {
        return (20 - 10) * 100u * p->weight_kg / 60 * spm / ACT_ACTIVE_SPM; // MET 2.0, part of a minute
    }
    return (met10(spm) - 10) * 100u * p->weight_kg / 60;
}

static uint32_t minute_cm(const activity_profile_t *p, uint32_t spm)
{
    return spm * activity_stride_cm(p, spm);
}

static uint16_t year_of(uint32_t day)
{
    return (uint16_t)(day / 10000);
}

void activity_day_summary(const activity_day_t *d, const activity_profile_t *p, uint16_t minute,
                          activity_summary_t *out)
{
    const uint32_t mins = minute >= ACT_MINUTES ? ACT_MINUTES : (uint32_t)minute + 1;
    const uint32_t bmr = d->t.day ? activity_bmr_kcal(p, year_of(d->t.day)) * mins / ACT_MINUTES : 0;
    *out = (activity_summary_t){
        .day = d->t.day,
        .steps = d->t.steps,
        .distance_m = d->t.distance_cm / 100,
        .active_kcal = d->t.active_mkcal / 1000,
        .kcal = d->t.active_mkcal / 1000 + bmr,
        .active_min = d->t.active_min,
        .step_goal = p->step_goal,
        .active_goal_min = p->active_goal_min,
    };
}

unsigned activity_day_add(activity_day_t *d, const activity_profile_t *p, uint32_t day, uint16_t minute,
                          uint32_t steps, activity_summary_t *ended)
{
    unsigned r = 0;
    if (day && d->t.day && day > d->t.day) {
        activity_day_summary(d, p, ACT_MINUTES, ended);
        activity_day_init(d, day);
        r |= ACT_DAY_ENDED;
    } else if (day && !d->t.day) {
        d->t.day = day; // the clock became known: what was counted belongs to today
    }
    if (steps == 0) {
        return r;
    }
    if (minute >= ACT_MINUTES) {
        minute = ACT_MINUTES - 1;
    }
    const uint32_t before = d->minute_steps[minute];
    uint32_t after = before + steps;
    if (after > UINT16_MAX) {
        after = UINT16_MAX;
    }
    d->minute_steps[minute] = (uint16_t)after;
    d->t.steps += steps;
    d->t.distance_cm += minute_cm(p, after) - minute_cm(p, before);
    // The minute's kcal is recomputed for its new cadence (MET steps up, never down).
    d->t.active_mkcal += activity_minute_mkcal(p, after) - activity_minute_mkcal(p, before);
    if (before < ACT_ACTIVE_SPM && after >= ACT_ACTIVE_SPM) {
        d->t.active_min++;
    }
    if (!d->t.step_goal_hit && p->step_goal && d->t.steps >= p->step_goal) {
        d->t.step_goal_hit = true;
        r |= ACT_STEP_GOAL;
    }
    if (!d->t.active_goal_hit && p->active_goal_min && d->t.active_min >= p->active_goal_min) {
        d->t.active_goal_hit = true;
        r |= ACT_ACTIVE_GOAL;
    }
    return r;
}
