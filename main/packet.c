#include "packet.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "pf.h"

static const char *TAG = "packet";

#define PF_DEBUG_LOGI(...)            \
    do {                              \
        if (pf_debug_enabled()) {     \
            ESP_LOGI(__VA_ARGS__);    \
        }                             \
    } while (0)

#define PF_DEBUG_LOGW(...)            \
    do {                              \
        if (pf_debug_enabled()) {     \
            ESP_LOGW(__VA_ARGS__);    \
        }                             \
    } while (0)

#define IEEE80211_MIN_FRAME_LEN 4
#define IEEE80211_MIN_SCORE 50
#define IEEE80211_LLC_HDR_LEN 8
#define PF_ETHERTYPE_EAPOL 0x888e

static pf_packet_list_t s_packet_history;
static pf_packet_t s_capture_packet;
static portMUX_TYPE s_packet_history_lock = portMUX_INITIALIZER_UNLOCKED;

static const uint32_t s_candidate_offsets[] = {
    0,  4,  8,  12, 16, 20, 24, 28, 32, 36, 40,
    44, 48, 52, 56, 60, 64, 68, 72, 76, 80,
};

static uint16_t read_le16(const uint8_t *p)
{
    return ((uint16_t)p[1] << 8) | p[0];
}

static uint16_t read_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static int score_80211_candidate(const uint8_t *p, uint32_t available_len)
{
    if (available_len < IEEE80211_MIN_FRAME_LEN) {
        return -100;
    }

    uint8_t fc0 = p[0];
    uint8_t fc1 = p[1];
    uint8_t version = fc0 & 0x03;
    uint8_t type = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0f;

    bool to_ds = (fc1 & 0x01) != 0;
    bool from_ds = (fc1 & 0x02) != 0;
    bool protected_frame = (fc1 & 0x40) != 0;

    int score = 0;

    score += (version == 0) ? 50 : -80;
    score += (type <= PF_PACKET_TYPE_DATA) ? 20 : -40;

    if (type == PF_PACKET_TYPE_MANAGEMENT) {
        if (subtype == 0 || subtype == 1 || subtype == 4 || subtype == 5 || subtype == 8 ||
            subtype == 10 || subtype == 11 || subtype == 12 || subtype == 13) {
            score += 30;
        }

        if (available_len >= 24) {
            score += 20;
        }
    }

    if (type == PF_PACKET_TYPE_CONTROL) {
        score += (subtype >= 8) ? 20 : -10;

        if (available_len >= 10) {
            score += 10;
        }
    }

    if (type == PF_PACKET_TYPE_DATA) {
        score += 10;

        if (available_len >= 24) {
            score += 15;
        }
    }

    if (fc0 == 0x80 && fc1 == 0x00) {
        score += 80;
    }

    if (fc0 == 0x50) {
        score += 50;
    }

    if (fc0 == 0xd4) {
        score += 40;
    }

    if (to_ds && from_ds) {
        score -= 5;
    }

    if (protected_frame) {
        score -= 3;
    }

    return score;
}

static bool find_80211_frame(const uint8_t *raw, uint32_t raw_len, uint32_t *out_offset)
{
    int best_score = -1000;
    uint32_t best_offset = 0;

    for (uint32_t i = 0; i < sizeof(s_candidate_offsets) / sizeof(s_candidate_offsets[0]); i++) {
        uint32_t offset = s_candidate_offsets[i];

        if (offset >= raw_len) {
            continue;
        }

        int score = score_80211_candidate(raw + offset, raw_len - offset);

        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }

    if (best_score < IEEE80211_MIN_SCORE) {
        return false;
    }

    *out_offset = best_offset;
    return true;
}

static void copy_addresses(pf_packet_t *packet, const uint8_t *frame)
{
    if (packet->frame_len < 24 ||
        (packet->type != PF_PACKET_TYPE_MANAGEMENT && packet->type != PF_PACKET_TYPE_DATA)) {
        packet->has_addresses = false;
        return;
    }

    memcpy(packet->addr1, frame + 4, sizeof(packet->addr1));
    memcpy(packet->addr2, frame + 10, sizeof(packet->addr2));
    memcpy(packet->addr3, frame + 16, sizeof(packet->addr3));
    packet->has_addresses = true;
}

static bool subtype_has_qos_control(const pf_packet_t *packet)
{
    return packet->type == PF_PACKET_TYPE_DATA &&
           (packet->subtype == 8 || packet->subtype == 9 || packet->subtype == 10 ||
            packet->subtype == 11 || packet->subtype == 12 || packet->subtype == 13 ||
            packet->subtype == 14 || packet->subtype == 15);
}

static uint32_t compute_payload_offset(const pf_packet_t *packet)
{
    uint32_t header_len = 24;

    if (packet->type == PF_PACKET_TYPE_CONTROL) {
        if (packet->subtype == 12 || packet->subtype == 13) {
            header_len = 10;
        } else {
            header_len = 16;
        }
    } else if (packet->type == PF_PACKET_TYPE_DATA) {
        if (packet->to_ds && packet->from_ds) {
            header_len = 30;
        }

        if (subtype_has_qos_control(packet)) {
            header_len += 2;
        }
    }

    return header_len;
}

static void parse_llc(pf_packet_t *packet, const uint8_t *frame)
{
    packet->has_llc = false;
    packet->llc_ethertype = 0;
    packet->is_eapol = false;
    packet->payload_offset = compute_payload_offset(packet);

    if (packet->type != PF_PACKET_TYPE_DATA || packet->frame_len < (packet->payload_offset + IEEE80211_LLC_HDR_LEN)) {
        return;
    }

    const uint8_t *llc = frame + packet->payload_offset;

    if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 0x03) {
        return;
    }

    packet->has_llc = true;
    packet->llc_ethertype = read_be16(llc + 6);
    packet->is_eapol = packet->llc_ethertype == PF_ETHERTYPE_EAPOL;
}

static bool should_store_raw(const pf_packet_t *packet, pf_capture_mode_t mode)
{
    if (mode == PF_CAPTURE_MODE_FULL) {
        return true;
    }

    if (mode == PF_CAPTURE_MODE_SMART) {
        return pf_should_capture_raw(packet);
    }

    return false;
}

static void maybe_store_raw(pf_packet_t *packet,
                            const uint8_t *raw,
                            uint32_t raw_len,
                            pf_capture_mode_t mode)
{
    packet->has_raw = false;
    packet->raw_stored_len = 0;

    if (!should_store_raw(packet, mode)) {
        return;
    }

    uint32_t copy_len = raw_len;

    if (copy_len > PF_PACKET_MAX_RAW_LEN) {
        copy_len = PF_PACKET_MAX_RAW_LEN;
    }

    memcpy(packet->raw, raw, copy_len);
    packet->has_raw = true;
    packet->raw_stored_len = copy_len;
}

static bool tagged_offset_for_management_frame(const pf_packet_t *packet, uint32_t *out_offset)
{
    if (packet == NULL || out_offset == NULL || packet->type != PF_PACKET_TYPE_MANAGEMENT) {
        return false;
    }

    if (packet->subtype == 8 || packet->subtype == 5) {
        *out_offset = 36;
        return true;
    }

    if (packet->subtype == 4) {
        *out_offset = 24;
        return true;
    }

    return false;
}

static bool channel_tag_looks_valid(uint8_t channel)
{
    return channel >= 1 && channel <= 13;
}
static bool management_frame_has_capabilities(const pf_packet_t *packet)
{
    return packet != NULL && packet->type == PF_PACKET_TYPE_MANAGEMENT &&
           (packet->subtype == 5 || packet->subtype == 8);
}

static bool vendor_tag_is_wpa(const uint8_t *tag, uint8_t tag_len)
{
    return tag != NULL && tag_len >= 4 && tag[0] == 0x00 && tag[1] == 0x50 &&
           tag[2] == 0xf2 && tag[3] == 0x01;
}

static bool suite_is_rsn(const uint8_t *suite)
{
    return suite != NULL && suite[0] == 0x00 && suite[1] == 0x0f && suite[2] == 0xac;
}

static pf_rsn_cipher_t rsn_cipher_from_suite(const uint8_t *suite)
{
    if (!suite_is_rsn(suite)) {
        return PF_RSN_CIPHER_UNKNOWN;
    }

    switch (suite[3]) {
        case 0:
            return PF_RSN_CIPHER_NONE;
        case 1:
            return PF_RSN_CIPHER_WEP40;
        case 2:
            return PF_RSN_CIPHER_TKIP;
        case 4:
            return PF_RSN_CIPHER_CCMP;
        case 5:
            return PF_RSN_CIPHER_WEP104;
        case 6:
            return PF_RSN_CIPHER_BIP_CMAC_128;
        case 8:
            return PF_RSN_CIPHER_GCMP;
        case 9:
            return PF_RSN_CIPHER_GCMP_256;
        case 10:
            return PF_RSN_CIPHER_CCMP_256;
        default:
            return PF_RSN_CIPHER_UNKNOWN;
    }
}

static pf_rsn_akm_t rsn_akm_from_suite(const uint8_t *suite)
{
    if (!suite_is_rsn(suite)) {
        return PF_RSN_AKM_UNKNOWN;
    }

    switch (suite[3]) {
        case 1:
            return PF_RSN_AKM_8021X;
        case 2:
            return PF_RSN_AKM_PSK;
        case 3:
            return PF_RSN_AKM_FT_8021X;
        case 4:
            return PF_RSN_AKM_FT_PSK;
        case 5:
            return PF_RSN_AKM_8021X_SHA256;
        case 6:
            return PF_RSN_AKM_PSK_SHA256;
        case 8:
            return PF_RSN_AKM_SAE;
        case 9:
            return PF_RSN_AKM_FT_SAE;
        case 18:
            return PF_RSN_AKM_OWE;
        default:
            return PF_RSN_AKM_UNKNOWN;
    }
}

static void parse_rsn_tag(pf_packet_t *packet, const uint8_t *tag, uint8_t tag_len)
{
    uint32_t offset = 0;
    uint16_t pairwise_count;
    uint16_t akm_count;

    packet->has_rsn = true;

    if (tag == NULL || tag_len < 8) {
        return;
    }

    packet->rsn_version = read_le16(tag + offset);
    offset += 2;

    packet->group_cipher = rsn_cipher_from_suite(tag + offset);
    offset += 4;

    if ((offset + 2) > tag_len) {
        return;
    }

    pairwise_count = read_le16(tag + offset);
    offset += 2;

    if (pairwise_count > 0 && (offset + 4) <= tag_len) {
        packet->pairwise_cipher = rsn_cipher_from_suite(tag + offset);
    }

    offset += (uint32_t)pairwise_count * 4;

    if ((offset + 2) > tag_len) {
        packet->has_rsn_details = true;
        return;
    }

    akm_count = read_le16(tag + offset);
    offset += 2;

    for (uint16_t i = 0; i < akm_count; i++) {
        pf_rsn_akm_t akm;

        if ((offset + 4) > tag_len) {
            break;
        }

        akm = rsn_akm_from_suite(tag + offset);

        if (packet->akm == PF_RSN_AKM_UNKNOWN || akm == PF_RSN_AKM_SAE || akm == PF_RSN_AKM_PSK) {
            packet->akm = akm;
        }

        if (akm == PF_RSN_AKM_SAE || akm == PF_RSN_AKM_FT_SAE) {
            packet->has_rsn_sae = true;
        }

        offset += 4;
    }

    if ((offset + 2) <= tag_len) {
        packet->rsn_capabilities = read_le16(tag + offset);
        packet->has_rsn_capabilities = true;
    }

    packet->has_rsn_details = true;
}

static void parse_management_capabilities(pf_packet_t *packet, const uint8_t *frame)
{
    packet->has_capability_info = false;
    packet->capability_info = 0;
    packet->privacy = false;

    if (!management_frame_has_capabilities(packet) || packet->frame_len < 36) {
        return;
    }

    packet->capability_info = read_le16(frame + 34);
    packet->has_capability_info = true;
    packet->privacy = (packet->capability_info & (1 << 4)) != 0;
}

static void parse_management_tags(pf_packet_t *packet, const uint8_t *frame)
{
    uint32_t tagged_offset;

    packet->has_ssid = false;
    packet->ssid[0] = '\0';
    packet->has_rsn = false;
    packet->has_wpa = false;
    packet->has_rsn_sae = false;
    packet->has_rsn_details = false;
    packet->rsn_version = 0;
    packet->group_cipher = PF_RSN_CIPHER_UNKNOWN;
    packet->pairwise_cipher = PF_RSN_CIPHER_UNKNOWN;
    packet->akm = PF_RSN_AKM_UNKNOWN;
    packet->has_rsn_capabilities = false;
    packet->rsn_capabilities = 0;
    parse_management_capabilities(packet, frame);

    if (!tagged_offset_for_management_frame(packet, &tagged_offset)) {
        return;
    }

    if (packet->frame_len <= tagged_offset) {
        return;
    }

    uint32_t offset = tagged_offset;

    while ((offset + 2) <= packet->frame_len) {
        uint8_t tag_id = frame[offset];
        uint8_t tag_len = frame[offset + 1];
        offset += 2;

        if ((offset + tag_len) > packet->frame_len) {
            return;
        }

        if (tag_id == 0) {
            uint8_t copy_len = tag_len;

            if (copy_len > 32) {
                copy_len = 32;
            }

            memcpy(packet->ssid, frame + offset, copy_len);
            packet->ssid[copy_len] = '\0';
            packet->has_ssid = tag_len > 0;
        } else if (tag_id == 3 && tag_len == 1 && channel_tag_looks_valid(frame[offset])) {
            packet->has_channel = true;
            packet->primary_channel = frame[offset];
            packet->secondary_channel = PF_SECONDARY_CHANNEL_NONE;
        } else if (tag_id == 48) {
            parse_rsn_tag(packet, frame + offset, tag_len);
        } else if (tag_id == 221 && vendor_tag_is_wpa(frame + offset, tag_len)) {
            packet->has_wpa = true;
        }

        offset += tag_len;
    }
}

static void log_packet(const pf_packet_t *packet)
{
    PF_DEBUG_LOGI(TAG,
                  "packet id=%lu raw_len=%lu frame_offset=%lu frame_len=%lu type=%s subtype=%s "
                  "to_ds=%u from_ds=%u retry=%u protected=%u",
                  packet->id,
                  packet->raw_len,
                  packet->frame_offset,
                  packet->frame_len,
                  pf_packet_type_name(packet),
                  pf_packet_subtype_name(packet),
                  packet->to_ds ? 1 : 0,
                  packet->from_ds ? 1 : 0,
                  packet->retry ? 1 : 0,
                  packet->protected_frame ? 1 : 0);

    if (packet->has_addresses) {
        PF_DEBUG_LOGI(TAG,
                      "addr1=%02x:%02x:%02x:%02x:%02x:%02x "
                      "addr2=%02x:%02x:%02x:%02x:%02x:%02x "
                      "addr3=%02x:%02x:%02x:%02x:%02x:%02x",
                      packet->addr1[0],
                      packet->addr1[1],
                      packet->addr1[2],
                      packet->addr1[3],
                      packet->addr1[4],
                      packet->addr1[5],
                      packet->addr2[0],
                      packet->addr2[1],
                      packet->addr2[2],
                      packet->addr2[3],
                      packet->addr2[4],
                      packet->addr2[5],
                      packet->addr3[0],
                      packet->addr3[1],
                      packet->addr3[2],
                      packet->addr3[3],
                      packet->addr3[4],
                      packet->addr3[5]);
    }
}

static void packet_copy_for_history(pf_packet_t *dst, const pf_packet_t *src)
{
    uint32_t raw_copy_len = src->raw_stored_len;

    dst->raw_len = src->raw_len;
    dst->raw_stored_len = src->raw_stored_len;
    dst->has_raw = src->has_raw;
    dst->has_rssi = src->has_rssi;
    dst->rssi = src->rssi;
    dst->has_channel = src->has_channel;
    dst->primary_channel = src->primary_channel;
    dst->secondary_channel = src->secondary_channel;
    dst->frame_offset = src->frame_offset;
    dst->frame_len = src->frame_len;
    dst->payload_offset = src->payload_offset;
    dst->frame_control = src->frame_control;
    dst->version = src->version;
    dst->type = src->type;
    dst->subtype = src->subtype;
    dst->type_name = src->type_name;
    dst->subtype_name = src->subtype_name;
    dst->to_ds = src->to_ds;
    dst->from_ds = src->from_ds;
    dst->retry = src->retry;
    dst->protected_frame = src->protected_frame;
    dst->has_addresses = src->has_addresses;
    memcpy(dst->addr1, src->addr1, sizeof(dst->addr1));
    memcpy(dst->addr2, src->addr2, sizeof(dst->addr2));
    memcpy(dst->addr3, src->addr3, sizeof(dst->addr3));
    dst->has_llc = src->has_llc;
    dst->llc_ethertype = src->llc_ethertype;
    dst->is_eapol = src->is_eapol;
    dst->has_ssid = src->has_ssid;
    memcpy(dst->ssid, src->ssid, sizeof(dst->ssid));
    dst->has_capability_info = src->has_capability_info;
    dst->capability_info = src->capability_info;
    dst->privacy = src->privacy;
    dst->has_rsn = src->has_rsn;
    dst->has_wpa = src->has_wpa;
    dst->has_rsn_sae = src->has_rsn_sae;
    dst->has_rsn_details = src->has_rsn_details;
    dst->rsn_version = src->rsn_version;
    dst->group_cipher = src->group_cipher;
    dst->pairwise_cipher = src->pairwise_cipher;
    dst->akm = src->akm;
    dst->has_rsn_capabilities = src->has_rsn_capabilities;
    dst->rsn_capabilities = src->rsn_capabilities;

    if (!src->has_raw) {
        dst->raw_stored_len = 0;
        return;
    }

    if (raw_copy_len > PF_PACKET_MAX_RAW_LEN) {
        raw_copy_len = PF_PACKET_MAX_RAW_LEN;
    }

    memcpy(dst->raw, src->raw, raw_copy_len);
    dst->raw_stored_len = raw_copy_len;
}

void pf_packet_list_init(pf_packet_list_t *list)
{
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
    list->next_id = 1;
}

bool pf_packet_list_push(pf_packet_list_t *list, const pf_packet_t *packet)
{
    size_t slot;

    if (list == NULL || packet == NULL) {
        return false;
    }

    if (list->count >= PF_PACKET_LIST_CAPACITY) {
        slot = list->start;
        list->start = (list->start + 1) % PF_PACKET_LIST_CAPACITY;
        list->dropped++;
    } else {
        slot = (list->start + list->count) % PF_PACKET_LIST_CAPACITY;
        list->count++;
    }

    packet_copy_for_history(&list->items[slot], packet);
    list->items[slot].id = list->next_id++;
    return true;
}

const pf_packet_t *pf_packet_list_get(const pf_packet_list_t *list, size_t index)
{
    size_t slot;

    if (list == NULL || index >= list->count) {
        return NULL;
    }

    slot = (list->start + index) % PF_PACKET_LIST_CAPACITY;
    return &list->items[slot];
}

bool pf_packet_parse_raw(pf_packet_t *packet, const uint8_t *raw, uint32_t raw_len)
{
    pf_packet_rx_info_t rx_info = {
        .has_rssi = false,
        .rssi = 0,
        .has_channel = false,
        .primary_channel = 0,
        .secondary_channel = PF_SECONDARY_CHANNEL_NONE,
        .capture_mode = PF_CAPTURE_MODE_METADATA_ONLY,
    };

    return pf_packet_parse_raw_ex(packet, raw, raw_len, &rx_info);
}

bool pf_packet_parse_raw_ex(pf_packet_t *packet,
                            const uint8_t *raw,
                            uint32_t raw_len,
                            const pf_packet_rx_info_t *rx_info)
{
    const uint8_t *frame;
    pf_capture_mode_t capture_mode = PF_CAPTURE_MODE_METADATA_ONLY;

    if (packet == NULL || raw == NULL || raw_len == 0 || raw_len > PF_PACKET_MAX_PARSE_LEN) {
        return false;
    }

    memset(packet, 0, sizeof(*packet));
    packet->raw_len = raw_len;
    packet->has_rssi = rx_info != NULL && rx_info->has_rssi;
    packet->rssi = packet->has_rssi ? rx_info->rssi : 0;
    packet->has_channel = rx_info != NULL && rx_info->has_channel;
    packet->primary_channel = packet->has_channel ? rx_info->primary_channel : 0;
    packet->secondary_channel = packet->has_channel ? rx_info->secondary_channel : PF_SECONDARY_CHANNEL_NONE;

    if (rx_info != NULL) {
        capture_mode = rx_info->capture_mode;
    }

    if (!find_80211_frame(raw, raw_len, &packet->frame_offset)) {
        return false;
    }

    frame = raw + packet->frame_offset;
    packet->frame_len = raw_len - packet->frame_offset;
    packet->frame_control = read_le16(frame);

    packet->version = packet->frame_control & 0x3;
    packet->type = (packet->frame_control >> 2) & 0x3;
    packet->subtype = (packet->frame_control >> 4) & 0xf;
    packet->to_ds = (packet->frame_control & (1 << 8)) != 0;
    packet->from_ds = (packet->frame_control & (1 << 9)) != 0;
    packet->retry = (packet->frame_control & (1 << 11)) != 0;
    packet->protected_frame = (packet->frame_control & (1 << 14)) != 0;
    packet->type_name = pf_packet_type_name(packet);
    packet->subtype_name = pf_packet_subtype_name(packet);

    copy_addresses(packet, frame);
    parse_management_tags(packet, frame);
    parse_llc(packet, frame);
    maybe_store_raw(packet, raw, raw_len, capture_mode);
    return true;
}

const char *pf_packet_type_name(const pf_packet_t *packet)
{
    if (packet == NULL) {
        return "Unknown";
    }

    switch (packet->type) {
        case PF_PACKET_TYPE_MANAGEMENT:
            return "Management";
        case PF_PACKET_TYPE_CONTROL:
            return "Control";
        case PF_PACKET_TYPE_DATA:
            return "Data";
        default:
            return "Reserved";
    }
}

const char *pf_packet_subtype_name(const pf_packet_t *packet)
{
    if (packet == NULL) {
        return "Unknown";
    }

    if (packet->type == PF_PACKET_TYPE_MANAGEMENT) {
        switch (packet->subtype) {
            case 0:
                return "AssociationRequest";
            case 1:
                return "AssociationResponse";
            case 4:
                return "ProbeRequest";
            case 5:
                return "ProbeResponse";
            case 8:
                return "Beacon";
            case 10:
                return "Disassociation";
            case 11:
                return "Authentication";
            case 12:
                return "Deauthentication";
            case 13:
                return "Action";
            default:
                return "ManagementOther";
        }
    }

    if (packet->type == PF_PACKET_TYPE_CONTROL) {
        switch (packet->subtype) {
            case 8:
                return "BlockAckRequest";
            case 9:
                return "BlockAck";
            case 10:
                return "PowerSavePoll";
            case 11:
                return "RTS";
            case 12:
                return "CTS";
            case 13:
                return "ACK";
            default:
                return "ControlOther";
        }
    }

    if (packet->type == PF_PACKET_TYPE_DATA) {
        switch (packet->subtype) {
            case 0:
                return "Data";
            case 4:
                return "NullData";
            case 8:
                return "QoSData";
            case 12:
                return "QoSNull";
            default:
                return "DataOther";
        }
    }

    return "Unknown";
}

bool pf_packet_is_type(const pf_packet_t *packet, pf_packet_type_t type)
{
    return packet != NULL && packet->type == type;
}

bool pf_packet_is_probe_request(const pf_packet_t *packet)
{
    return packet != NULL && packet->type == PF_PACKET_TYPE_MANAGEMENT && packet->subtype == 4;
}

bool pf_packet_is_probe_response(const pf_packet_t *packet)
{
    return packet != NULL && packet->type == PF_PACKET_TYPE_MANAGEMENT && packet->subtype == 5;
}

bool pf_packet_is_data(const pf_packet_t *packet)
{
    return packet != NULL && packet->type == PF_PACKET_TYPE_DATA;
}

bool pf_packet_is_eapol(const pf_packet_t *packet)
{
    return packet != NULL && packet->is_eapol;
}

void pf_packet_history_clear(void)
{
    portENTER_CRITICAL(&s_packet_history_lock);
    pf_packet_list_init(&s_packet_history);
    portEXIT_CRITICAL(&s_packet_history_lock);
}

const pf_packet_list_t *pf_packet_history(void)
{
    return &s_packet_history;
}

size_t pf_packet_history_count(void)
{
    size_t count;

    portENTER_CRITICAL(&s_packet_history_lock);
    count = s_packet_history.count;
    portEXIT_CRITICAL(&s_packet_history_lock);

    return count;
}

static void packet_to_metadata(const pf_packet_t *packet, pf_packet_metadata_t *metadata)
{
    metadata->id = packet->id;
    metadata->raw_len = packet->raw_len;
    metadata->raw_stored_len = packet->raw_stored_len;
    metadata->has_raw = packet->has_raw;
    metadata->has_rssi = packet->has_rssi;
    metadata->rssi = packet->rssi;
    metadata->has_channel = packet->has_channel;
    metadata->primary_channel = packet->primary_channel;
    metadata->secondary_channel = packet->secondary_channel;
    metadata->frame_offset = packet->frame_offset;
    metadata->frame_len = packet->frame_len;
    metadata->payload_offset = packet->payload_offset;
    metadata->frame_control = packet->frame_control;
    metadata->version = packet->version;
    metadata->type = packet->type;
    metadata->subtype = packet->subtype;
    metadata->type_name = packet->type_name;
    metadata->subtype_name = packet->subtype_name;
    metadata->to_ds = packet->to_ds;
    metadata->from_ds = packet->from_ds;
    metadata->retry = packet->retry;
    metadata->protected_frame = packet->protected_frame;
    metadata->has_addresses = packet->has_addresses;
    memcpy(metadata->addr1, packet->addr1, sizeof(metadata->addr1));
    memcpy(metadata->addr2, packet->addr2, sizeof(metadata->addr2));
    memcpy(metadata->addr3, packet->addr3, sizeof(metadata->addr3));
    metadata->has_llc = packet->has_llc;
    metadata->llc_ethertype = packet->llc_ethertype;
    metadata->is_eapol = packet->is_eapol;
    metadata->has_ssid = packet->has_ssid;
    memcpy(metadata->ssid, packet->ssid, sizeof(metadata->ssid));
    metadata->has_capability_info = packet->has_capability_info;
    metadata->capability_info = packet->capability_info;
    metadata->privacy = packet->privacy;
    metadata->has_rsn = packet->has_rsn;
    metadata->has_wpa = packet->has_wpa;
    metadata->has_rsn_sae = packet->has_rsn_sae;
    metadata->has_rsn_details = packet->has_rsn_details;
    metadata->rsn_version = packet->rsn_version;
    metadata->group_cipher = packet->group_cipher;
    metadata->pairwise_cipher = packet->pairwise_cipher;
    metadata->akm = packet->akm;
    metadata->has_rsn_capabilities = packet->has_rsn_capabilities;
    metadata->rsn_capabilities = packet->rsn_capabilities;
}

static bool history_slot_for_index(size_t index, size_t *out_slot)
{
    if (out_slot == NULL || index >= s_packet_history.count) {
        return false;
    }

    *out_slot = (s_packet_history.start + index) % PF_PACKET_LIST_CAPACITY;
    return true;
}

bool pf_packet_history_get_copy(size_t index, pf_packet_t *out)
{
    size_t slot;

    if (out == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_packet_history_lock);

    if (!history_slot_for_index(index, &slot)) {
        portEXIT_CRITICAL(&s_packet_history_lock);
        return false;
    }

    *out = s_packet_history.items[slot];

    portEXIT_CRITICAL(&s_packet_history_lock);
    return true;
}

bool pf_packet_history_get_metadata(size_t index, pf_packet_metadata_t *out)
{
    size_t slot;

    if (out == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_packet_history_lock);

    if (!history_slot_for_index(index, &slot)) {
        portEXIT_CRITICAL(&s_packet_history_lock);
        return false;
    }

    packet_to_metadata(&s_packet_history.items[slot], out);

    portEXIT_CRITICAL(&s_packet_history_lock);
    return true;
}

bool pf_packet_history_capture(const uint8_t *raw,
                               uint32_t raw_len,
                               const pf_packet_rx_info_t *rx_info,
                               bool verbose)
{
    if (!pf_packet_parse_raw_ex(&s_capture_packet, raw, raw_len, rx_info)) {
        if (verbose) {
            PF_DEBUG_LOGW(TAG, "raw buffer did not contain a supported 802.11 packet, len=%lu", raw_len);
        }

        return false;
    }

    portENTER_CRITICAL(&s_packet_history_lock);
    pf_packet_list_push(&s_packet_history, &s_capture_packet);
    portEXIT_CRITICAL(&s_packet_history_lock);

    if (verbose) {
        size_t count = pf_packet_history_count();

        log_packet(&s_capture_packet);
        PF_DEBUG_LOGI(TAG,
                      "packet history count=%u",
                      (unsigned)count);
    }

    return true;
}
