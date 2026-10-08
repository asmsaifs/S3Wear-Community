// sys_core: event bus, UI mailbox, task/lock wrappers, logging policy.
#pragma once

#include "esp_err.h"
#include "s3w_event.h"
#include "s3w_log.h"
#include "s3w_task.h"
#include "s3w_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Boot step 3 (docs/02 §2): logging policy, UI mailbox, event bus. */
esp_err_t sys_core_init(void);

#ifdef __cplusplus
}
#endif
