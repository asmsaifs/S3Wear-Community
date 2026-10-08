#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Removable card. Internal flash (LittleFS at /flash) is mounted by svc_storage
// through the VFS and needs no HAL call on either platform.

/** Mount the card's FAT volume at base_path. Never formats. Fails if no card. */
esp_err_t hal_sd_mount(const char *base_path);
esp_err_t hal_sd_unmount(void);
bool hal_sd_is_mounted(void);
/** Mounted and still answering (a pulled card returns false). */
bool hal_sd_present(void);

#ifdef __cplusplus
}
#endif
