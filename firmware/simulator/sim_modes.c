/* Simulator modes (sim_modes.h). */
#include "sim_modes.h"

#include <stdio.h>
#include <time.h>

#include "sim_script.h"

static modes_t s_modes;

static void now_local(int *wday, int *min)
{
    const time_t t = sim_fixed_now();
    struct tm lt;
    gmtime_r(&t, &lt); /* the simulator runs in UTC0 */
    *wday = lt.tm_wday;
    *min = lt.tm_hour * 60 + lt.tm_min;
}

modes_state_t sim_modes_state(void)
{
    int wday;
    int min;
    now_local(&wday, &min);
    return modes_eval(&s_modes, wday, min);
}

void sim_modes_set(mode_id_t id, bool on)
{
    int wday;
    int min;
    now_local(&wday, &min);
    modes_set(&s_modes, id, on, wday, min);
    printf("modes: %s %s\n", mode_name(id), on ? "on" : "off");
}

void sim_modes_sched(mode_id_t id, const mode_sched_t *sched)
{
    if (id != MODE_THEATER && (unsigned)id < MODE_COUNT) {
        s_modes.sched[id] = *sched;
    }
}
