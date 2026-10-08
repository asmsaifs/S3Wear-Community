// Complication registry (docs/03-firmware-features.md F1): what a face slot can show,
// which data it depends on and which app a tap opens. wf_comp_render() turns the data
// into a small view model (value, caption, gauge); the LVGL widgets that draw it are
// in the engine. Pure C, no LVGL.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wf_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WF_COMP_NONE = 0,
    WF_COMP_BATTERY,
    WF_COMP_STEPS,
    WF_COMP_DATE,
    WF_COMP_WEATHER,
    WF_COMP_NEXT_EVENT,
    WF_COMP_SUNRISE, // next sunrise or sunset
    WF_COMP_WORLD_TIME,
    WF_COMP_PHONE_BATTERY,
    WF_COMP_HEART_RATE,
    WF_COMP_ALARM,
    WF_COMP_TIMER,
    WF_COMP_NOTIFICATIONS,
    WF_COMP_MOON,
    WF_COMP_COUNT,
} wf_comp_t;

typedef struct {
    const char *id;   // stable name: face.json "default", FaceConfig, console
    const char *name; // human name (picker, toast)
    const char *app;  // screen id a tap opens (ui_nav_push_id); NULL = none
    uint32_t deps;    // wf_data_mask_t bits that change it
} wf_comp_info_t;

/** Registry entry; NULL for an out-of-range value. WF_COMP_NONE is "none". */
const wf_comp_info_t *wf_comp_info(wf_comp_t c);

/** By id; WF_COMP_COUNT if unknown ("none" is WF_COMP_NONE). */
wf_comp_t wf_comp_find(const char *id);

typedef struct {
    char value[24];  // main text: "82%", "6,420", "18°", "10:30"; "--" if unknown
    char label[12];  // caption, at most 5 upper-case letters: "BATT", "STEPS", "SAT"
    char detail[40]; // longer text for wide slots: "Standup", "Waxing gibbous"; may be ""
    int16_t ratio;   // 0..1000 for a gauge ring, -1 = no gauge
    uint32_t color;  // 0xRRGGBB for the value and the gauge
    bool known;      // data present
} wf_comp_view_t;

/** Render c for ctx (WF_COMP_NONE gives an empty view). */
void wf_comp_render(wf_comp_t c, const wf_ctx_t *ctx, wf_comp_view_t *out);

#ifdef __cplusplus
}
#endif
