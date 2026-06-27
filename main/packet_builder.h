#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tx.h"

typedef struct {
    uint8_t bytes[PF_TX_MAX_FRAME_LEN];
    uint32_t len;
    bool ok;
} pf_frame_t;

typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
    uint16_t beacon_interval;
    uint16_t capability_info;
    const char *country;
} pf_beacon_options_t;

typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
} pf_probe_request_options_t;

typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
    uint16_t beacon_interval;
    uint16_t capability_info;
} pf_probe_response_options_t;

typedef struct {
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence;
    uint16_t reason_code;
} pf_deauth_options_t;

pf_frame_t Beacon(const char *ssid,
                  const uint8_t sender_mac[6],
                  uint8_t channel,
                  const pf_beacon_options_t *options);
pf_frame_t ProbeRequest(const char *ssid,
                        const uint8_t sender_mac[6],
                        uint8_t channel,
                        const pf_probe_request_options_t *options);
pf_frame_t ProbeResponse(const char *ssid,
                         const uint8_t sender_mac[6],
                         uint8_t channel,
                         const pf_probe_response_options_t *options);
pf_frame_t Deauth(const uint8_t sender_mac[6],
                  uint8_t channel,
                  const pf_deauth_options_t *options);

bool pf_get_esp_wifi_mac(uint8_t out_mac[6]);
bool pf_get_esp_packet_mac(uint8_t out_mac[6]);
bool pf_frame_set_sequence(pf_frame_t *frame, uint16_t sequence);
