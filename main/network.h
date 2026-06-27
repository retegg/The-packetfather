#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "packet.h"

#ifndef PF_NETWORK_LIST_CAPACITY
#define PF_NETWORK_LIST_CAPACITY 32
#endif

typedef enum {
    PF_NETWORK_SECURITY_UNKNOWN = 0,
    PF_NETWORK_SECURITY_OPEN,
    PF_NETWORK_SECURITY_WEP,
    PF_NETWORK_SECURITY_WPA,
    PF_NETWORK_SECURITY_WPA2,
    PF_NETWORK_SECURITY_WPA3,
} pf_network_security_t;

typedef struct {
    bool in_use;
    uint8_t bssid[6];

    bool has_ssid;
    bool hidden_ssid;
    char ssid[33];

    bool has_channel;
    uint8_t primary_channel;

    bool has_rssi;
    int8_t last_rssi;
    int16_t average_rssi;

    bool privacy;
    bool has_rsn;
    bool has_wpa;
    bool has_rsn_sae;
    bool has_rsn_details;
    uint16_t rsn_version;
    pf_rsn_cipher_t group_cipher;
    pf_rsn_cipher_t pairwise_cipher;
    pf_rsn_akm_t akm;
    bool has_rsn_capabilities;
    uint16_t rsn_capabilities;
    pf_network_security_t security;

    uint32_t beacon_count;
    uint32_t probe_response_count;
    uint32_t first_seen_packet_id;
    uint32_t last_seen_packet_id;
} pf_network_t;

typedef struct {
    pf_network_t items[PF_NETWORK_LIST_CAPACITY];
    size_t count;
    size_t dropped;
} pf_network_list_t;

void pf_network_list_init(pf_network_list_t *list);
bool pf_network_list_update_from_packet(pf_network_list_t *list, const pf_packet_metadata_t *packet);
size_t pf_network_list_count(const pf_network_list_t *list);
bool pf_network_list_get_copy(const pf_network_list_t *list, size_t index, pf_network_t *out);
const char *pf_network_security_name(pf_network_security_t security);
const char *pf_rsn_cipher_name(pf_rsn_cipher_t cipher);
const char *pf_rsn_akm_name(pf_rsn_akm_t akm);
