// Hello → HelloAck handler for svc_link (P4-06 handshake).
#pragma once
#include "esp_err.h"

/** After svc_link_start(). */
esp_err_t link_hello_register(void);
