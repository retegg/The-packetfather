#pragma once

#include <stdbool.h>

#include "packet.h"

typedef bool (*pf_capture_selector_t)(const pf_packet_t *packet, void *ctx);

bool pf_init(void);
void pf_debug(bool enabled);
bool pf_debug_enabled(void);
void pf_set_capture_mode(pf_capture_mode_t mode);
pf_capture_mode_t pf_get_capture_mode(void);
void pf_set_capture_selector(pf_capture_selector_t selector, void *ctx);
bool pf_should_capture_raw(const pf_packet_t *packet);
const pf_packet_list_t *pf_sniff(void);
void pf_sniff_stop(void);
void pf_clear_packets(void);
