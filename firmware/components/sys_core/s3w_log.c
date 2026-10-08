#include "s3w_log.h"

#include "esp_log.h"

// Third-party components that log at INFO on every codec open, radio start, etc.
static const char *const k_quiet[] = {
    "I2S_IF", "Adev_Codec", "ES8311", "ES7210", "NimBLE", "BLE_INIT", "wifi", "wifi_init",
    "pp", "net80211", "phy_init", "coexist",
};

void s3w_log_init(void)
{
    for (size_t i = 0; i < sizeof k_quiet / sizeof k_quiet[0]; i++) {
        esp_log_level_set(k_quiet[i], ESP_LOG_WARN);
    }
}
