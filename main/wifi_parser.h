#pragma once

/*
 * Minimal IEEE 802.11 frame parsing helpers.
 *
 * The parser reads a caller-owned raw frame buffer and logs basic metadata. It
 * does not retain the buffer or modify it.
 */

#include <stddef.h>
#include <stdint.h>

void wifi_parser_handle_80211(const uint8_t *buf, size_t len);
