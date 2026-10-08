// Countdown timers (timer_set.h). Pure C.
#include "timer_set.h"

#include <string.h>

void timer_set_init(timer_set_t *t)
{
    memset(t, 0, sizeof *t);
}

countdown_t *timer_set_find(timer_set_t *t, uint8_t id)
{
    for (int i = 0; id && i < t->count; i++) {
        if (t->items[i].id == id) {
            return &t->items[i];
        }
    }
    return NULL;
}

uint8_t timer_set_start(timer_set_t *t, uint32_t duration_ms, uint32_t now)
{
    if (t->count >= TIMER_MAX || duration_ms == 0 || duration_ms > TIMER_DURATION_MAX) {
        return 0;
    }
    uint8_t id = t->last_id;
    do {
        id++;
    } while (id == 0 || timer_set_find(t, id));
    t->last_id = id;
    t->items[t->count++] = (countdown_t){
        .id = id,
        .state = TIMER_RUNNING,
        .duration_ms = duration_ms,
        .end_ms = now + duration_ms,
    };
    return id;
}

uint32_t timer_left_ms(const countdown_t *c, uint32_t now)
{
    switch ((timer_state_t)c->state) {
    case TIMER_RUNNING: {
        const int32_t left = (int32_t)(c->end_ms - now);
        return left > 0 ? (uint32_t)left : 0;
    }
    case TIMER_PAUSED:
        return c->left_ms;
    default:
        return 0;
    }
}

bool timer_set_pause(timer_set_t *t, uint8_t id, uint32_t now)
{
    countdown_t *c = timer_set_find(t, id);
    if (c == NULL || c->state != TIMER_RUNNING) {
        return false;
    }
    c->left_ms = timer_left_ms(c, now);
    c->state = TIMER_PAUSED;
    return true;
}

bool timer_set_resume(timer_set_t *t, uint8_t id, uint32_t now)
{
    countdown_t *c = timer_set_find(t, id);
    if (c == NULL || c->state != TIMER_PAUSED) {
        return false;
    }
    c->end_ms = now + c->left_ms;
    c->state = TIMER_RUNNING;
    return true;
}

bool timer_set_restart(timer_set_t *t, uint8_t id, uint32_t now)
{
    countdown_t *c = timer_set_find(t, id);
    if (c == NULL) {
        return false;
    }
    c->end_ms = now + c->duration_ms;
    c->left_ms = 0;
    c->state = TIMER_RUNNING;
    return true;
}

bool timer_set_remove(timer_set_t *t, uint8_t id)
{
    countdown_t *c = timer_set_find(t, id);
    if (c == NULL) {
        return false;
    }
    const size_t i = (size_t)(c - t->items);
    memmove(c, c + 1, (t->count - i - 1) * sizeof *c);
    t->count--;
    return true;
}

bool timer_set_next_end(const timer_set_t *t, uint32_t *end_ms)
{
    bool found = false;
    uint32_t best = 0;
    for (int i = 0; i < t->count; i++) {
        const countdown_t *c = &t->items[i];
        if (c->state == TIMER_RUNNING && (!found || (int32_t)(c->end_ms - best) < 0)) {
            best = c->end_ms;
            found = true;
        }
    }
    if (found) {
        *end_ms = best;
    }
    return found;
}

size_t timer_set_expire(timer_set_t *t, uint32_t now, uint8_t *ids, size_t max)
{
    size_t n = 0;
    for (int i = 0; i < t->count; i++) {
        countdown_t *c = &t->items[i];
        if (c->state == TIMER_RUNNING && (int32_t)(now - c->end_ms) >= 0) {
            c->state = TIMER_DONE;
            if (n < max && ids) {
                ids[n] = c->id;
            }
            n++;
        }
    }
    return n;
}
