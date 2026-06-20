#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PF_PACKET_MAX_RAW_LEN
#define PF_PACKET_MAX_RAW_LEN 1600
#endif

#ifndef PF_PACKET_MAX_PARSE_LEN
#define PF_PACKET_MAX_PARSE_LEN 4096
#endif

#ifndef PF_PACKET_LIST_CAPACITY
#define PF_PACKET_LIST_CAPACITY 16
#endif

typedef enum {
    PF_PACKET_TYPE_MANAGEMENT = 0,
    PF_PACKET_TYPE_CONTROL = 1,
    PF_PACKET_TYPE_DATA = 2,
    PF_PACKET_TYPE_RESERVED = 3,
} pf_packet_type_t;

typedef enum {
    PF_SECONDARY_CHANNEL_NONE = 0,
    PF_SECONDARY_CHANNEL_ABOVE = 1,
    PF_SECONDARY_CHANNEL_BELOW = 2,
} pf_secondary_channel_t;

typedef enum {
    PF_CAPTURE_MODE_METADATA_ONLY = 0,
    PF_CAPTURE_MODE_SMART = 1,
    PF_CAPTURE_MODE_FULL = 2,
} pf_capture_mode_t;

typedef enum {
    PF_RSN_CIPHER_UNKNOWN = 0,
    PF_RSN_CIPHER_NONE,
    PF_RSN_CIPHER_WEP40,
    PF_RSN_CIPHER_TKIP,
    PF_RSN_CIPHER_CCMP,
    PF_RSN_CIPHER_WEP104,
    PF_RSN_CIPHER_BIP_CMAC_128,
    PF_RSN_CIPHER_GCMP,
    PF_RSN_CIPHER_GCMP_256,
    PF_RSN_CIPHER_CCMP_256,
} pf_rsn_cipher_t;

typedef enum {
    PF_RSN_AKM_UNKNOWN = 0,
    PF_RSN_AKM_8021X,
    PF_RSN_AKM_PSK,
    PF_RSN_AKM_FT_8021X,
    PF_RSN_AKM_FT_PSK,
    PF_RSN_AKM_8021X_SHA256,
    PF_RSN_AKM_PSK_SHA256,
    PF_RSN_AKM_SAE,
    PF_RSN_AKM_FT_SAE,
    PF_RSN_AKM_OWE,
} pf_rsn_akm_t;

typedef struct {
    bool has_rssi;
    int8_t rssi;
    bool has_channel;
    uint8_t primary_channel;
    pf_secondary_channel_t secondary_channel;
    pf_capture_mode_t capture_mode;
} pf_packet_rx_info_t;

typedef struct {
    uint32_t id;

    uint8_t raw[PF_PACKET_MAX_RAW_LEN];
    uint32_t raw_len;
    uint32_t raw_stored_len;
    bool has_raw;

    bool has_rssi;
    int8_t rssi;
    bool has_channel;
    uint8_t primary_channel;
    pf_secondary_channel_t secondary_channel;

    uint32_t frame_offset;
    uint32_t frame_len;
    uint32_t payload_offset;

    uint16_t frame_control;
    uint8_t version;
    uint8_t type;
    uint8_t subtype;
    const char *type_name;
    const char *subtype_name;

    bool to_ds;
    bool from_ds;
    bool retry;
    bool protected_frame;

    bool has_addresses;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];

    bool has_llc;
    uint16_t llc_ethertype;
    bool is_eapol;

    bool has_ssid;
    char ssid[33];

    bool has_capability_info;
    uint16_t capability_info;
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
} pf_packet_t;

typedef struct {
    pf_packet_t items[PF_PACKET_LIST_CAPACITY];
    size_t start;
    size_t count;
    size_t dropped;
    uint32_t next_id;
} pf_packet_list_t;

typedef struct {
    uint32_t id;
    uint32_t raw_len;
    uint32_t raw_stored_len;
    bool has_raw;
    bool has_rssi;
    int8_t rssi;
    bool has_channel;
    uint8_t primary_channel;
    pf_secondary_channel_t secondary_channel;
    uint32_t frame_offset;
    uint32_t frame_len;
    uint32_t payload_offset;
    uint16_t frame_control;
    uint8_t version;
    uint8_t type;
    uint8_t subtype;
    const char *type_name;
    const char *subtype_name;
    bool to_ds;
    bool from_ds;
    bool retry;
    bool protected_frame;
    bool has_addresses;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    bool has_llc;
    uint16_t llc_ethertype;
    bool is_eapol;
    bool has_ssid;
    char ssid[33];

    bool has_capability_info;
    uint16_t capability_info;
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
} pf_packet_metadata_t;

void pf_packet_list_init(pf_packet_list_t *list);
bool pf_packet_list_push(pf_packet_list_t *list, const pf_packet_t *packet);
const pf_packet_t *pf_packet_list_get(const pf_packet_list_t *list, size_t index);

bool pf_packet_parse_raw(pf_packet_t *packet, const uint8_t *raw, uint32_t raw_len);
bool pf_packet_parse_raw_ex(pf_packet_t *packet,
                            const uint8_t *raw,
                            uint32_t raw_len,
                            const pf_packet_rx_info_t *rx_info);

const char *pf_packet_type_name(const pf_packet_t *packet);
const char *pf_packet_subtype_name(const pf_packet_t *packet);

bool pf_packet_is_type(const pf_packet_t *packet, pf_packet_type_t type);
bool pf_packet_is_probe_request(const pf_packet_t *packet);
bool pf_packet_is_probe_response(const pf_packet_t *packet);
bool pf_packet_is_data(const pf_packet_t *packet);
bool pf_packet_is_eapol(const pf_packet_t *packet);

void pf_packet_history_clear(void);
const pf_packet_list_t *pf_packet_history(void);
size_t pf_packet_history_count(void);
bool pf_packet_history_get_copy(size_t index, pf_packet_t *out);
bool pf_packet_history_get_metadata(size_t index, pf_packet_metadata_t *out);
bool pf_packet_history_capture(const uint8_t *raw,
                               uint32_t raw_len,
                               const pf_packet_rx_info_t *rx_info,
                               bool verbose);
