#include "raise_detect.h"

#include <math.h>
#include <stddef.h>

#define DEG_TO_RAD 0.017453292f
#define ONE_G_MG   1000.0f

uint8_t raise_cone_deg(int sensitivity)
{
    switch (sensitivity) {
    case 0:
        return RAISE_CONE_LOW_DEG;
    case 2:
        return RAISE_CONE_HIGH_DEG;
    default:
        return RAISE_CONE_MID_DEG;
    }
}

void raise_init(raise_detect_t *r, uint8_t cone_deg)
{
    *r = (raise_detect_t){
        .cos_cone = cosf((float)cone_deg * DEG_TO_RAD),
        .cos_arm = cosf((float)(cone_deg + RAISE_ARM_MARGIN_DEG) * DEG_TO_RAD),
        .min_cos = 1.0f,
        .last_cos = 1.0f,
    };
}

void raise_motion(raise_detect_t *r, uint32_t now)
{
    if (!r->open) {
        r->open = true;
        // The window opens after the wrist started moving (wake-on-motion latency, light-sleep
        // wake, sensor settle: ~200 ms), often past the away pose of a quick raise: arm from
        // the last known pose (the previous window's last sample, or raise_pose()).
        r->armed = r->last_cos <= r->cos_arm;
        r->have_prev = false;
        r->in_still = false;
        r->start_ms = now;
        r->min_cos = r->last_cos;
        r->samples = 0;
    }
    r->last_motion_ms = now;
}

void raise_pose(raise_detect_t *r, int32_t x, int32_t y, int32_t z)
{
    const float mag = sqrtf((float)x * x + (float)y * y + (float)z * z);
    r->last_cos = mag > 1.0f ? (float)z / mag : 1.0f;
}

void raise_cancel(raise_detect_t *r)
{
    r->open = false;
}

bool raise_window_open(const raise_detect_t *r)
{
    return r->open;
}

raise_result_t raise_sample(raise_detect_t *r, int32_t x, int32_t y, int32_t z, uint32_t now)
{
    if (!r->open) {
        return RAISE_END;
    }
    if (r->samples < UINT16_MAX) {
        r->samples++;
    }
    const float mag = sqrtf((float)x * x + (float)y * y + (float)z * z);
    const float c = mag > 1.0f ? (float)z / mag : 0.0f;
    r->last_cos = c;
    if (c < r->min_cos) {
        r->min_cos = c;
    }
    if (c <= r->cos_arm) {
        r->armed = true;
    }

    bool still = fabsf(mag - ONE_G_MG) <= RAISE_STILL_MAG_MG;
    if (r->have_prev) {
        const float dx = (float)(x - r->prev[0]);
        const float dy = (float)(y - r->prev[1]);
        const float dz = (float)(z - r->prev[2]);
        still = still && sqrtf(dx * dx + dy * dy + dz * dz) <= RAISE_STILL_DELTA_MG;
    }
    r->prev[0] = x;
    r->prev[1] = y;
    r->prev[2] = z;
    r->have_prev = true;
    if (!still) {
        r->last_motion_ms = now;
    }

    if (still && c >= r->cos_cone) {
        if (!r->in_still) {
            r->in_still = true;
            r->still_since_ms = now;
        }
        if (r->armed && now - r->still_since_ms >= RAISE_STABLE_MS) {
            r->open = false;
            return RAISE_WAKE;
        }
    } else {
        r->in_still = false;
    }

    if (now - r->last_motion_ms >= RAISE_QUIET_MS || now - r->start_ms >= RAISE_MAX_MS) {
        r->open = false;
        return RAISE_END;
    }
    return RAISE_CONTINUE;
}

int raise_angle_deg(int32_t x, int32_t y, int32_t z)
{
    const float mag = sqrtf((float)x * x + (float)y * y + (float)z * z);
    if (mag < 1.0f) {
        return 90;
    }
    float c = (float)z / mag;
    c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
    return (int)lroundf(acosf(c) / DEG_TO_RAD);
}
