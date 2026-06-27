#pragma once

#include <stdbool.h>
#include <stdint.h>

bool wifi_tx_raw(const uint8_t *frame, uint32_t len);