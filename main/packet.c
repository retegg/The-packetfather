#include "packet.h"

#include <string.h>

#include "esp_log.h"
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

static void parse_ssid(pf_packet_t *packet, const uint8_t *frame)
{
    uint32_t tagged_offset;

    packet->has_ssid = false;
    packet->ssid[0] = '\0';

    if (packet->type != PF_PACKET_TYPE_MANAGEMENT) {
        return;
    }

    if (packet->subtype == 8 || packet->subtype == 5) {
        tagged_offset = 36;
    } else if (packet->subtype == 4) {
        tagged_offset = 24;
    } else {
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
            return;
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

    pf_packet_t stored = *packet;
    stored.id = list->next_id++;

    list->items[slot] = stored;
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

    if (packet == NULL || raw == NULL || raw_len == 0 || raw_len > PF_PACKET_MAX_RAW_LEN) {
        return false;
    }

    memset(packet, 0, sizeof(*packet));
    packet->raw_len = raw_len;
    packet->has_rssi = rx_info != NULL && rx_info->has_rssi;
    packet->rssi = packet->has_rssi ? rx_info->rssi : 0;

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
    parse_ssid(packet, frame);
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
    pf_packet_list_init(&s_packet_history);
}

const pf_packet_list_t *pf_packet_history(void)
{
    return &s_packet_history;
}

bool pf_packet_history_capture(const uint8_t *raw,
                               uint32_t raw_len,
                               const pf_packet_rx_info_t *rx_info,
                               bool verbose)
{
    pf_packet_t packet;

    if (!pf_packet_parse_raw_ex(&packet, raw, raw_len, rx_info)) {
        if (verbose) {
            PF_DEBUG_LOGW(TAG, "raw buffer did not contain a supported 802.11 packet, len=%lu", raw_len);
        }

        return false;
    }

    pf_packet_list_push(&s_packet_history, &packet);

    if (verbose) {
        log_packet(&packet);
        PF_DEBUG_LOGI(TAG,
                      "packet history count=%u dropped=%u",
                      (unsigned)s_packet_history.count,
                      (unsigned)s_packet_history.dropped);
    }

    return true;
}
