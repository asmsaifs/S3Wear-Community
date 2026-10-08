// Host stand-in for ESP-IDF's esp_err.h (host tests and simulator): the codes pure-logic code returns, with IDF's values.
#pragma once

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK                0
#define ESP_FAIL              -1
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE  0x104
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT       0x107
