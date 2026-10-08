#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ft_result.h"

/** Init NimBLE, advertise (non-connectable) for adv_ms, then tear the stack down. */
ft_status_t ft_ble_advertise(uint32_t adv_ms, char *detail, size_t len);

/** Init Wi-Fi STA, one blocking scan, then stop and deinit Wi-Fi. */
ft_status_t ft_wifi_scan(char *detail, size_t len);
