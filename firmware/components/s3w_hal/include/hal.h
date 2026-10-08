// Hardware abstraction layer (docs/02-firmware-architecture.md §1). Services, the UI
// and watch faces use these interfaces, never bsp_s3w or drv_* directly, so the same
// code runs on the watch (hal/s3w/, over bsp_s3w) and in the simulator
// (simulator/hal_sim/). Headers use only standard C types and esp_err_t.
//
// Exceptions that stay on the BSP: app_main (board bring-up order), svc_diag and
// factory_test (hardware diagnostics, target-only).
#pragma once

#include "hal_display.h"
#include "hal_imu.h"
#include "hal_input.h"
#include "hal_pmu.h"
#include "hal_power.h"
#include "hal_rtc.h"
#include "hal_storage.h"
