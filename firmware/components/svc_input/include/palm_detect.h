// Palm-cover detection for svc_input (docs/03 F3: touch area > 60 % of the screen ->
// screen off). Pure logic, no ESP-IDF: built by firmware/host_test.
//
// The FT3168 reports at most two tracked points, each with a coarse contact area,
// plus a point count that goes beyond what it tracks (or out of range) for a large
// contact. A sample "covers" the screen when any of these holds:
//   - the per-point contact area reaches area_min;
//   - the controller reports more than max_points contacts (a blob, not fingers);
//   - two or more points span a bounding box of at least cover_pct % of the screen.
// Covering samples for hold_ms without a break -> one palm event; then nothing more
// until every contact is lifted. Thresholds are tuned on hardware (`input` console
// command prints the last contact).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One touch controller sample (also the release: points = 0). */
typedef struct {
    uint8_t points;   // contacts reported by the controller (may exceed what it tracks)
    uint8_t area_max; // largest per-point contact area, controller units (0 = unknown)
    uint16_t x_min;   // bounding box of the tracked points, display pixels
    uint16_t y_min;
    uint16_t x_max;
    uint16_t y_max;
} palm_sample_t;

typedef struct {
    uint16_t width; // screen, pixels
    uint16_t height;
    uint8_t cover_pct;  // bounding box share of the screen
    uint8_t area_min;   // per-point area that counts as a palm (0: not used)
    uint8_t max_points; // more contacts than this is a palm
    uint32_t hold_ms;   // covering this long -> palm
} palm_cfg_t;

typedef struct {
    palm_cfg_t cfg;
    bool covering;
    bool fired;       // palm reported for this contact; wait for release
    uint32_t t_cover; // first covering sample of the current run
} palm_detect_t;

void palm_detect_init(palm_detect_t *p, const palm_cfg_t *cfg);
/** True if this sample alone looks like a palm. */
bool palm_sample_covers(const palm_cfg_t *cfg, const palm_sample_t *s);
/** Feed every sample in order; true exactly once per palm contact. */
bool palm_detect_feed(palm_detect_t *p, const palm_sample_t *s, uint32_t now);

#ifdef __cplusplus
}
#endif
