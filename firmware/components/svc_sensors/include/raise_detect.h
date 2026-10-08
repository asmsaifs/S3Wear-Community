// Raise-to-wake gesture detector (docs/03-firmware-features.md F2). Pure logic, no
// ESP-IDF: built by firmware/host_test.
//
// svc_sensors opens a window when the IMU's wake-on-motion fires and feeds it
// accelerometer samples in the watch frame (mg: +x toward 3 o'clock, +y toward 12
// o'clock, +z out of the screen; lying face up at rest reads z = +1000). A raise is:
//   1. the screen pointed clearly away from the user: the gravity vector was at least
//      RAISE_ARM_MARGIN_DEG outside the screen-up cone during the window or in the last
//      known pose before it (the window opens late in a quick raise), then
//   2. it entered the cone (half angle cone_deg around +z), and
//   3. it stayed there with the watch still (|a| near 1 g, small change between
//      samples) for RAISE_STABLE_MS.
// Starting inside the cone and moving within it (typing, steering) never wakes.
// The window ends RAISE_QUIET_MS after the last movement, or RAISE_MAX_MS after it
// opened.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAISE_CONE_LOW_DEG  25 // RAISE_SENSITIVITY 0
#define RAISE_CONE_MID_DEG  35 // 1 (default, docs/03 F2: ±35°)
#define RAISE_CONE_HIGH_DEG 45 // 2
#define RAISE_ARM_MARGIN_DEG 15   // "clearly away": this far outside the cone
#define RAISE_STABLE_MS      60   // still inside the cone this long -> wake
#define RAISE_STILL_DELTA_MG 120  // change between two samples counted as still
#define RAISE_STILL_MAG_MG   250  // | |a| - 1000 mg | counted as still
#define RAISE_QUIET_MS       1000 // window ends this long after the last movement
#define RAISE_MAX_MS         3000 // and never lasts longer than this

typedef enum {
    RAISE_CONTINUE = 0, // keep sampling
    RAISE_WAKE,         // raise detected: wake the screen; the window is over
    RAISE_END,          // window over without a raise
} raise_result_t;

typedef struct {
    float cos_cone; // in cone: z / |a| >= cos_cone
    float cos_arm;  // clearly away: z / |a| <= cos_arm
    bool open;
    bool armed;     // was clearly away during this window
    bool have_prev;
    int32_t prev[3];
    uint32_t start_ms;
    uint32_t last_motion_ms;
    uint32_t still_since_ms; // in cone and still since (valid while in_still)
    bool in_still;
    // Last window, for the console
    float min_cos;  // smallest z / |a| seen
    float last_cos; // latest z / |a|: the last known pose (1 = unknown, counted as screen up)
    uint16_t samples;
} raise_detect_t;

/** Cone half angle from the RAISE_SENSITIVITY setting (0 low .. 2 high; out of range = mid). */
uint8_t raise_cone_deg(int sensitivity);

void raise_init(raise_detect_t *r, uint8_t cone_deg);

/** Wake-on-motion fired at now: opens a window, or extends the open one. */
void raise_motion(raise_detect_t *r, uint32_t now);

/** The pose outside a window (mg, watch frame), e.g. when the screen goes off: a window that
 *  opens next is armed if it was clearly away. */
void raise_pose(raise_detect_t *r, int32_t x, int32_t y, int32_t z);

/** One sample (mg, watch frame) at now. Outside a window: RAISE_END. */
raise_result_t raise_sample(raise_detect_t *r, int32_t x, int32_t y, int32_t z, uint32_t now);

/** Close the window (screen turned on another way, detection disabled). */
void raise_cancel(raise_detect_t *r);

bool raise_window_open(const raise_detect_t *r);

/** Angle in degrees between a and +z (screen up), 0..180; 90 for a zero vector. */
int raise_angle_deg(int32_t x, int32_t y, int32_t z);

#ifdef __cplusplus
}
#endif
