#pragma once

#include <stdbool.h>
#include <stdint.h>

bool wifi_rx_find_80211_frame(const uint8_t *buf, uint32_t len, uint32_t *out_offset);
void wifi_rx_analyze_packet(const uint8_t *buf, uint32_t len, bool verbose);
