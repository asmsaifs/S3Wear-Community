#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Logging policy: one TAG per file; debug logs are compiled out of release builds
 * (sdkconfig.defaults.release caps LOG_MAXIMUM_LEVEL at INFO). Here: quieten chatty
 * third-party tags so our own logs stay readable on the console.
 */
void s3w_log_init(void);

#ifdef __cplusplus
}
#endif
