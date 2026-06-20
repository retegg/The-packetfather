#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef PF_TX_MAX_FRAME_LEN
#define PF_TX_MAX_FRAME_LEN 512
#endif

bool pf_tx_raw(const uint8_t *frame, uint32_t len);
