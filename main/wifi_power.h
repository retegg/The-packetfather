#pragma once

/*
 * Low-level Wi-Fi/RF power bring-up.
 *
 * This does not start the normal ESP-IDF Wi-Fi stack or join a network.
 */

void wifi_power_enable_minimal(void);
void wifi_power_disable_minimal(void);
