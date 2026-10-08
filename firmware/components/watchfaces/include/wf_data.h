// Watch face data model (docs/03-firmware-features.md F1): the values faces and
// complications may show. Pure C, no LVGL. Services fill it on the UI task through
// wf_data_edit()/wf_data_changed() (wf_engine.h); faces read it through wf_ctx_t.
// Anything not known yet (no phone, no service) stays at its "unknown" value and is
// shown as "--".
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What changed (wf_data_changed(), face update callbacks). */
typedef enum {
    WF_DATA_TIME = 1u << 0,    // minute tick, display on, time/zone/format/valid change
    WF_DATA_SECOND = 1u << 1,  // once a second, only for WF_FACE_SECONDS faces while not AOD
    WF_DATA_BATTERY = 1u << 2,
    WF_DATA_STEPS = 1u << 3,
    WF_DATA_WEATHER = 1u << 4,
    WF_DATA_PHONE = 1u << 5,
    WF_DATA_EVENT = 1u << 6,
    WF_DATA_SUN = 1u << 7,
    WF_DATA_WORLD = 1u << 8,
    WF_DATA_HEART = 1u << 9,
    WF_DATA_ALARM = 1u << 10,
    WF_DATA_TIMER = 1u << 11,
    WF_DATA_NOTIFY = 1u << 12,
    WF_DATA_ALL = 0x1FFFu,
} wf_data_mask_t;

typedef enum {
    WF_WEATHER_CLEAR,
    WF_WEATHER_CLOUDY,
    WF_WEATHER_RAIN,
    WF_WEATHER_SNOW,
    WF_WEATHER_STORM,
    WF_WEATHER_FOG,
    WF_WEATHER_COUNT,
} wf_weather_t;

typedef struct {
    int8_t battery_pct;       // -1 unknown
    bool charging;
    int32_t steps;            // -1 unknown
    int32_t steps_goal;       // > 0
    bool weather_valid;
    int16_t temp_c;           // current, °C
    int16_t temp_lo_c;
    int16_t temp_hi_c;
    uint8_t weather;          // wf_weather_t
    int8_t phone_battery_pct; // -1 unknown / not connected
    bool event_valid;
    char event_title[40];
    time_t event_start;
    int16_t sunrise_min;      // local minutes after midnight, -1 unknown
    int16_t sunset_min;
    bool world_valid;
    char world_label[8];      // short upper-case city label, e.g. "TOKYO"
    int32_t world_utc_offset_s;
    uint8_t heart_bpm;        // 0 unknown
    time_t alarm_next;        // 0 = no alarm set
    time_t timer_end;         // 0 = no timer running
    int16_t notifications;    // unread count, -1 unknown
} wf_data_t;

/** Everything unknown (steps goal 10 000). */
void wf_data_init(wf_data_t *d);

/** Snapshot for one render: data plus the wall clock. */
typedef struct {
    const wf_data_t *data;
    time_t now;     // UTC
    struct tm tm;   // local time of now
    bool h24;       // 24 h clock (TIME_24H)
    bool time_valid;
} wf_ctx_t;

/** Fill ctx->tm from now (localtime_r) and set the other fields. */
void wf_ctx_init(wf_ctx_t *ctx, const wf_data_t *data, time_t now, bool h24, bool time_valid);

#ifdef __cplusplus
}
#endif
