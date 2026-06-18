/*
 * Raw RX buffer analyzer.
 *
 * Wi-Fi DMA currently returns an internal metadata prefix followed by the
 * probable 802.11 frame. This module scores candidate offsets and forwards the
 * best plausible frame to the parser.
 */

#include "wifi_rx_analyzer.h"
#include "wifi_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "wifi_rx_analyzer";

#define IEEE80211_TYPE_MGMT 0
#define IEEE80211_TYPE_CTRL 1
#define IEEE80211_TYPE_DATA 2

#define IEEE80211_MIN_FRAME_LEN 4
#define IEEE80211_MIN_SCORE 50

static const uint32_t s_candidate_offsets[] = {
    0,  4,  8,  12, 16, 20, 24, 28, 32, 36, 40,
    44, 48, 52, 56, 60, 64, 68, 72, 76, 80,
};

static const char *type_to_string(uint8_t type)
{
    switch (type) {
        case IEEE80211_TYPE_MGMT:
            return "mgmt";
        case IEEE80211_TYPE_CTRL:
            return "ctrl";
        case IEEE80211_TYPE_DATA:
            return "data";
        default:
            return "reserved";
    }
}

static const char *subtype_to_string(uint8_t type, uint8_t subtype)
{
    if (type == IEEE80211_TYPE_MGMT) {
        switch (subtype) {
            case 0:
                return "assoc_req";
            case 1:
                return "assoc_resp";
            case 2:
                return "reassoc_req";
            case 3:
                return "reassoc_resp";
            case 4:
                return "probe_req";
            case 5:
                return "probe_resp";
            case 8:
                return "beacon";
            case 9:
                return "atim";
            case 10:
                return "disassoc";
            case 11:
                return "auth";
            case 12:
                return "deauth";
            case 13:
                return "action";
            default:
                return "mgmt_other";
        }
    }

    if (type == IEEE80211_TYPE_CTRL) {
        switch (subtype) {
            case 8:
                return "block_ack_req";
            case 9:
                return "block_ack";
            case 10:
                return "ps_poll";
            case 11:
                return "rts";
            case 12:
                return "cts";
            case 13:
                return "ack";
            case 14:
                return "cf_end";
            case 15:
                return "cf_end_ack";
            default:
                return "ctrl_other";
        }
    }

    if (type == IEEE80211_TYPE_DATA) {
        switch (subtype) {
            case 0:
                return "data";
            case 4:
                return "null";
            case 8:
                return "qos_data";
            case 12:
                return "qos_null";
            default:
                return "data_other";
        }
    }

    return "unknown";
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
    score += (type <= IEEE80211_TYPE_DATA) ? 20 : -40;

    if (type == IEEE80211_TYPE_MGMT) {
        if (subtype == 0 || subtype == 1 || subtype == 4 || subtype == 5 || subtype == 8 ||
            subtype == 10 || subtype == 11 || subtype == 12 || subtype == 13) {
            score += 30;
        }

        if (available_len >= 24) {
            score += 20;
        }
    }

    if (type == IEEE80211_TYPE_CTRL) {
        score += (subtype >= 8) ? 20 : -10;

        if (available_len >= 10) {
            score += 10;
        }
    }

    if (type == IEEE80211_TYPE_DATA) {
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

bool wifi_rx_find_80211_frame(const uint8_t *buf, uint32_t len, uint32_t *out_offset)
{
    if (buf == NULL || out_offset == NULL) {
        return false;
    }

    int best_score = -1000;
    uint32_t best_offset = 0;

    for (uint32_t i = 0; i < sizeof(s_candidate_offsets) / sizeof(s_candidate_offsets[0]); i++) {
        uint32_t offset = s_candidate_offsets[i];

        if (offset >= len) {
            continue;
        }

        int score = score_80211_candidate(buf + offset, len - offset);

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

static void dump_at_offset(const uint8_t *buf, uint32_t len, uint32_t offset)
{
    if (buf == NULL || offset >= len) {
        return;
    }

    uint32_t dump_len = len - offset;

    if (dump_len > 48) {
        dump_len = 48;
    }

    ESP_LOGW(TAG, "dump offset=%lu len=%lu dump_len=%lu", offset, len, dump_len);

    for (uint32_t i = 0; i < dump_len; i += 16) {
        char line[96];
        int pos = 0;

        pos += snprintf(line + pos, sizeof(line) - pos, "%04lu: ", offset + i);

        for (uint32_t j = 0; j < 16 && (i + j) < dump_len; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02x ", buf[offset + i + j]);
        }

        ESP_LOGW(TAG, "%s", line);
    }
}

void wifi_rx_analyze_packet(const uint8_t *buf, uint32_t len, bool verbose)
{
    if (buf == NULL) {
        return;
    }

    uint32_t offset = 0;

    if (!wifi_rx_find_80211_frame(buf, len, &offset)) {
        if (verbose) {
            ESP_LOGW(TAG, "no valid 802.11 frame offset found, len=%lu", len);
            dump_at_offset(buf, len, 0);
        }

        return;
    }

    const uint8_t *frame = buf + offset;
    uint32_t frame_len = len - offset;

    uint8_t fc0 = frame[0];
    uint8_t fc1 = frame[1];

    uint8_t version = fc0 & 0x03;
    uint8_t type = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0f;

    bool to_ds = (fc1 & 0x01) != 0;
    bool from_ds = (fc1 & 0x02) != 0;
    bool retry = (fc1 & 0x08) != 0;
    bool protected_frame = (fc1 & 0x40) != 0;

    ESP_LOGI(TAG,
             "RX 802.11 offset=%lu frame_len=%lu ver=%u type=%s subtype=%s "
             "raw_subtype=%u to_ds=%u from_ds=%u retry=%u protected=%u",
             offset,
             frame_len,
             version,
             type_to_string(type),
             subtype_to_string(type, subtype),
             subtype,
             to_ds,
             from_ds,
             retry,
             protected_frame);

    if (verbose) {
        dump_at_offset(buf, len, offset);
    }

    wifi_parser_handle_80211(frame, frame_len);
}
