// Settings service: typed access to the settings declared in settings_schema.h,
// persisted in NVS (namespace "s3w_set"). Getters and setters are cheap and safe from
// any task, including the UI task: a set updates RAM, posts SVC_SETTINGS_EVT_CHANGED
// and queues the NVS write on svc_worker, so callers never wait for flash.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "settings_schema.h"
#include "settings_store.h"
#include "svc_settings_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Boot step 3 (after sys_core_init and svc_worker_start): NVS flash init (erased and
 * re-initialised if full or from a newer format) and load every setting.
 */
esp_err_t svc_settings_init(void);

/** Default for an unknown id or the wrong type: 0 / false. */
int32_t svc_settings_get_int(s3w_setting_t id);
bool svc_settings_get_bool(s3w_setting_t id);

/** Copies the value (NUL-terminated). ESP_ERR_INVALID_SIZE if buf is too small; ESP_ERR_INVALID_ARG if not a string. */
esp_err_t svc_settings_get_str(s3w_setting_t id, char *buf, size_t len);

/**
 * ESP_ERR_INVALID_ARG: unknown id, wrong type, out of range. ESP_ERR_INVALID_SIZE:
 * string longer than its max_len. Setting the current value is a no-op (no event, no write).
 */
esp_err_t svc_settings_set_int(s3w_setting_t id, int32_t v);
esp_err_t svc_settings_set_bool(s3w_setting_t id, bool v);
esp_err_t svc_settings_set_str(s3w_setting_t id, const char *v);

/** Back to the schema default (posts CHANGED if it differed). */
esp_err_t svc_settings_reset(s3w_setting_t id);

/**
 * Every setting back to its default and the "s3w_set" namespace erased (on svc_worker).
 * Other NVS namespaces (PHY calibration, BLE bonds) are not touched: the full factory
 * reset flow (F19) clears bonds and storage separately. Posts SVC_SETTINGS_EVT_RESET.
 */
esp_err_t svc_settings_factory_reset(void);

/** Settings changed in RAM whose NVS write is still queued or failed (diagnostics). */
int svc_settings_pending(void);

#ifdef __cplusplus
}
#endif
