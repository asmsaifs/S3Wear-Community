// Storage service: internal LittleFS at /flash (power-loss safe) and the optional
// TF card (FAT) at /sd. The SD card is optional everywhere: no card, a removed card
// or a bad card must never stop the watch (docs/01-hardware.md §6).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_STORAGE_FLASH_PATH "/flash"
#define SVC_STORAGE_SD_PATH    "/sd"

/** Mount LittleFS (partition "storage") at /flash; formats on first boot / corruption. */
esp_err_t svc_storage_mount_flash(void);

/** Queue the SD mount on svc_worker (boot step 6). Returns immediately. */
esp_err_t svc_storage_start(void);

esp_err_t svc_storage_sd_mount(void);
esp_err_t svc_storage_sd_unmount(void);

/**
 * Mounted and the card still answers. If it was mounted but no longer answers
 * (pulled out), it is unmounted and SVC_STORAGE_EVT_SD_REMOVED is posted.
 */
bool svc_storage_sd_check(void);

/** Bytes for /flash or /sd. */
esp_err_t svc_storage_usage(const char *mount_path, uint64_t *total, uint64_t *used);

#ifdef __cplusplus
}
#endif
