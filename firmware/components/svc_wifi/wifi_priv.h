// svc_wifi internals shared between its files.
#pragma once

#include "esp_err.h"

/** wifi_link.c: WifiConfig handler and WifiStatus events (from svc_wifi_start()). */
esp_err_t wifi_link_start(void);
