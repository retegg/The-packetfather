#pragma once

#include <stdbool.h>

#include "packet.h"

bool pf_init(void);
const pf_packet_list_t *pf_sniff(void);
void pf_sniff_stop(void);
void pf_clear_packets(void);
