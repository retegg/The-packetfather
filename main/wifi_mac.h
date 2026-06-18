#pragma once

/*
 * Experimental local RX/filter register helpers.
 *
 * These functions do not transmit frames or modify traffic on the air.
 */

void wifi_mac_rx_enable_experimental(void);
void wifi_mac_dump_extra_registers(void);
void wifi_mac_rx_bit_probe_task(void *arg);
