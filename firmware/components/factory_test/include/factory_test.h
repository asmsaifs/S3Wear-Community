// Factory test (P1-09): exercises every chip on the board, shows a live checklist on
// the AMOLED and prints one JSON line with the results on the console.
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run all tests and block until done (~1 min with the interactive steps). Interactive
 * steps (press BOOT, press PWR, tap the screen) are SKIPped when interactive = false.
 * Prints the JSON result line to stdout. Returns ESP_OK if every item is PASS/SKIP.
 */
esp_err_t factory_test_run(bool interactive);

#ifdef __cplusplus
}
#endif
