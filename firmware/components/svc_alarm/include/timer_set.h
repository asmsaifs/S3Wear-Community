// Countdown timers (docs/03-firmware-features.md F6): several at once, each running,
// paused or done. Pure logic, no ESP-IDF: built by firmware/host_test and the
// simulator. Times are a caller-supplied monotonic millisecond clock (esp_timer on
// the watch, the LVGL tick in the simulator); differences are wrap-safe. Timers are
// not persisted: a reboot ends them.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMER_MAX         6
#define TIMER_DURATION_MAX (24u * 3600u * 1000u - 1000u) // 23:59:59

typedef enum {
    TIMER_RUNNING,
    TIMER_PAUSED,
    TIMER_DONE, // reached zero; stays until removed or restarted
} timer_state_t;

typedef struct {
    uint8_t id;           // 1..255
    uint8_t state;        // timer_state_t
    uint32_t duration_ms; // as started
    uint32_t end_ms;      // RUNNING: clock value at zero
    uint32_t left_ms;     // PAUSED: time left
} countdown_t;

typedef struct {
    countdown_t items[TIMER_MAX]; // in start order
    uint8_t count;
    uint8_t last_id;
} timer_set_t;

void timer_set_init(timer_set_t *t);

/** Start a new timer; its id, or 0 if full or duration is 0 or above the maximum. */
uint8_t timer_set_start(timer_set_t *t, uint32_t duration_ms, uint32_t now);

countdown_t *timer_set_find(timer_set_t *t, uint8_t id);

/** RUNNING -> PAUSED; false otherwise. */
bool timer_set_pause(timer_set_t *t, uint8_t id, uint32_t now);
/** PAUSED -> RUNNING; false otherwise. */
bool timer_set_resume(timer_set_t *t, uint8_t id, uint32_t now);
/** Any state -> RUNNING with the full duration again. */
bool timer_set_restart(timer_set_t *t, uint8_t id, uint32_t now);
bool timer_set_remove(timer_set_t *t, uint8_t id);

/** Time left (0 when done). */
uint32_t timer_left_ms(const countdown_t *c, uint32_t now);

/** Earliest running timer: true and its clock value at zero in *end_ms. */
bool timer_set_next_end(const timer_set_t *t, uint32_t *end_ms);

/** Running timers that reached zero become DONE; their ids go to ids (up to max).
 *  Returns how many finished. */
size_t timer_set_expire(timer_set_t *t, uint32_t now, uint8_t *ids, size_t max);

#ifdef __cplusplus
}
#endif
