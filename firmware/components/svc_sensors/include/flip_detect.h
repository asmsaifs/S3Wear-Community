// Flip detector (docs/03-firmware-features.md F2/F6: flip to snooze an alarm, later
// to mute calls). Pure logic, no ESP-IDF: built by firmware/host_test.
//
// Accelerometer samples in the watch frame (mg, +z out of the screen; face up at
// rest reads z = +1000). A flip is: the screen was not facing down (armed), then it
// faces down (z <= -FLIP_DOWN_MG) with the watch still (|a| near 1 g) for
// FLIP_HOLD_MS. A watch that already lies face down when detection starts has to be
// turned up first, so it cannot snooze by itself.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLIP_DOWN_MG   800 // face down: z at most -800 mg (within ~37 deg of straight down)
#define FLIP_ARM_MG    300 // armed once z is above -300 mg (not facing down)
#define FLIP_STILL_MG  300 // | |a| - 1000 mg | counted as still
#define FLIP_HOLD_MS   400 // face down and still this long -> flip

typedef struct {
    bool armed;
    bool down;           // face down and still since down_since_ms
    uint32_t down_since_ms;
    bool fired;          // reported; needs to be armed again for another flip
} flip_detect_t;

void flip_init(flip_detect_t *f);

/** One sample at now; true once per flip. */
bool flip_sample(flip_detect_t *f, int32_t x, int32_t y, int32_t z, uint32_t now);

#ifdef __cplusplus
}
#endif
