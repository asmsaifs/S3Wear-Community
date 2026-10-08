// Screenshot of the watch's own screen (docs/03 F17, P8-17): what the panel shows now, toasts and
// banners included, saved as a PNG in /sd/shots when a card is mounted, else /flash/shots.
//
// Files are named shot_YYYYMMDD_HHMMSS.png (local time; shot_u<uptime s>.png before the clock is
// set). /flash keeps the newest SVC_SCREENSHOT_FLASH_KEEP (by name), the card keeps everything.
// The console (`screenshot`, docs/02 §11) and the phone (`ScreenshotRequest`) trigger it; there is
// no on-watch trigger yet.
//
// Blocks the caller for up to a second and needs a 400 KB PSRAM buffer for the length of the
// call, so call it from the console or a worker task, never from the UI task.
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_SCREENSHOT_DIR_FLASH  "/flash/shots"
#define SVC_SCREENSHOT_DIR_SD     "/sd/shots"
#define SVC_SCREENSHOT_FLASH_KEEP 8
#define SVC_SCREENSHOT_RESERVE_FLASH (2 * 1024 * 1024) // free bytes /flash keeps for everything else

/**
 * Capture and save. On success path (may be NULL) gets the file's full path.
 * ESP_ERR_INVALID_STATE: screen off or called on the UI task; ESP_ERR_NO_MEM: no capture buffer
 * or /flash is down to its reserve; ESP_ERR_TIMEOUT: no frame in time; ESP_FAIL: file error.
 */
esp_err_t svc_screenshot_take(char *path, size_t path_len);

/** Boot step (after svc_link_start and svc_worker_start): answers the phone's ScreenshotRequest. */
esp_err_t svc_screenshot_start(void);

/**
 * Capture and upload to the phone (TRANSFER_SCREENSHOT_UP, docs/06 §4 "Screenshot"); nothing is
 * saved on the watch. Returns once the job is queued; the capture and upload follow and are logged.
 * Any task but the UI task. ESP_ERR_INVALID_STATE: no phone or screen off; ESP_ERR_NOT_FINISHED:
 * the previous one is still on its way.
 */
esp_err_t svc_screenshot_send(void);

/** Directory screenshots go to right now: SVC_SCREENSHOT_DIR_SD if the card answers, else flash. */
const char *svc_screenshot_dir(void);

#ifdef __cplusplus
}
#endif
