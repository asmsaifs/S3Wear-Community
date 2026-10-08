// AOD burn-in protection (docs/01-hardware.md §5): the AOD face moves by up to ±4 px,
// one step per minute. Pure C (host test test_wf_shift).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_AOD_SHIFT_MAX_PX 4
#define WF_AOD_SHIFT_PERIOD 48 // minutes until the pattern repeats

/**
 * Offset of the AOD face for a minute count (minutes since the epoch, so it is the
 * same after a rebuild). A snake over the 5 x 5 grid of even offsets -4..4, there
 * and back: each minute moves 2 px along one axis, every offset is visited, and
 * over a period the mean offset is about 0.
 */
void wf_aod_shift(uint32_t minute, int8_t *dx, int8_t *dy);

#ifdef __cplusplus
}
#endif
