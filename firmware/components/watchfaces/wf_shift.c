#include "wf_shift.h"

#define GRID 5 // offsets -4, -2, 0, 2, 4
#define STEP 2

void wf_aod_shift(uint32_t minute, int8_t *dx, int8_t *dy)
{
    uint32_t i = minute % WF_AOD_SHIFT_PERIOD; // 0..24 there, 25..47 back (24 and 0 not repeated)
    if (i >= GRID * GRID) {
        i = WF_AOD_SHIFT_PERIOD - i;
    }
    const uint32_t row = i / GRID;
    uint32_t col = i % GRID;
    if (row & 1) {
        col = GRID - 1 - col;
    }
    *dx = (int8_t)((int)col * STEP - WF_AOD_SHIFT_MAX_PX);
    *dy = (int8_t)((int)row * STEP - WF_AOD_SHIFT_MAX_PX);
}
