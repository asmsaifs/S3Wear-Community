// DeviceStatus events to the companion (P4-06): battery, charging, storage, time valid.
#pragma once
#include "esp_err.h"

/** After svc_link_start() and svc_power_start(): sends on every battery change while the link is up. */
esp_err_t link_status_register(void);

/** Send a DeviceStatus now (any task). ESP_ERR_INVALID_STATE when the link is down. */
esp_err_t link_status_send(void);
