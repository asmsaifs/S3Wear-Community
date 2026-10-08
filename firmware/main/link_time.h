// TimeSync handler for svc_link (P4-06): the phone sets time, zone and 12/24 h.
#pragma once
#include <stdint.h>
#include "esp_err.h"

/** After svc_link_start() and svc_time_start(). */
esp_err_t link_time_register(void);

/** Seconds since the last successful TimeSync from the phone; -1 if none since boot. Any task. */
int32_t link_time_sync_age_s(void);
