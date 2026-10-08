#include "palm_detect.h"

#include <string.h>

void palm_detect_init(palm_detect_t *p, const palm_cfg_t *cfg)
{
    memset(p, 0, sizeof *p);
    p->cfg = *cfg;
}

bool palm_sample_covers(const palm_cfg_t *cfg, const palm_sample_t *s)
{
    if (s->points == 0) {
        return false;
    }
    if (cfg->area_min > 0 && s->area_max >= cfg->area_min) {
        return true;
    }
    if (s->points > cfg->max_points) {
        return true;
    }
    if (s->points < 2 || s->x_max < s->x_min || s->y_max < s->y_min) {
        return false;
    }
    const uint64_t box = (uint64_t)(s->x_max - s->x_min + 1) * (uint64_t)(s->y_max - s->y_min + 1);
    const uint64_t screen = (uint64_t)cfg->width * cfg->height;
    return box * 100 >= screen * cfg->cover_pct;
}

bool palm_detect_feed(palm_detect_t *p, const palm_sample_t *s, uint32_t now)
{
    if (s->points == 0) {
        p->covering = false;
        p->fired = false;
        return false;
    }
    if (p->fired) {
        return false;
    }
    if (!palm_sample_covers(&p->cfg, s)) {
        p->covering = false;
        return false;
    }
    if (!p->covering) {
        p->covering = true;
        p->t_cover = now;
    }
    if (now - p->t_cover >= p->cfg.hold_ms) {
        p->fired = true;
        return true;
    }
    return false;
}
