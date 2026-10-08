// Flip detector (flip_detect.h). Pure C.
#include "flip_detect.h"

#include <math.h>
#include <string.h>

void flip_init(flip_detect_t *f)
{
    memset(f, 0, sizeof *f);
}

bool flip_sample(flip_detect_t *f, int32_t x, int32_t y, int32_t z, uint32_t now)
{
    if (z > -FLIP_ARM_MG) {
        f->armed = true;
        f->fired = false;
        f->down = false;
        return false;
    }
    const float mag = sqrtf((float)x * (float)x + (float)y * (float)y + (float)z * (float)z);
    const bool down = z <= -FLIP_DOWN_MG && fabsf(mag - 1000.0f) <= FLIP_STILL_MG;
    if (!down || !f->armed || f->fired) {
        f->down = false;
        return false;
    }
    if (!f->down) {
        f->down = true;
        f->down_since_ms = now;
        return false;
    }
    if (now - f->down_since_ms >= FLIP_HOLD_MS) {
        f->fired = true;
        f->armed = false;
        return true;
    }
    return false;
}
