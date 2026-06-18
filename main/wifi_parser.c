/*
 * Minimal IEEE 802.11 frame parser.
 *
 * The parser is intentionally conservative: it validates only the fields needed
 * by the current RX experiment and logs basic frame metadata.
 */

#include "wifi_parser.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "wifi_parser";

static uint16_t read_le16(const uint8_t *p)
{
    return ((uint16_t)p[1] << 8) | p[0];
}

static const char *frame_type_to_string(uint8_t type)
{
    switch (type) {
        case 0:
            return "mgmt";
        case 1:
            return "ctrl";
        case 2:
            return "data";
        default:
            return "reserved";
    }
}

static const char *frame_subtype_to_string(uint8_t type, uint8_t subtype)
{
    if (type == 0) {
        switch (subtype) {
            case 0:
                return "assoc_req";
            case 1:
                return "assoc_resp";
            case 4:
                return "probe_req";
            case 5:
                return "probe_resp";
            case 8:
                return "beacon";
            case 10:
                return "disassoc";
            case 11:
                return "auth";
            case 12:
                return "deauth";
            default:
                return "mgmt_other";
        }
    }

    if (type == 1) {
        switch (subtype) {
            case 11:
                return "rts";
            case 12:
                return "cts";
            case 13:
                return "ack";
            default:
                return "ctrl_other";
        }
    }

    if (type == 2) {
        switch (subtype) {
            case 0:
                return "data";
            case 4:
                return "null_data";
            case 8:
                return "qos_data";
            default:
                return "data_other";
        }
    }

    return "unknown";
}

static void format_mac(char out[18], const uint8_t *mac)
{
    snprintf(out,
             18,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

void wifi_parser_handle_80211(const uint8_t *buf, size_t len)
{
    if (buf == NULL) {
        ESP_LOGW(TAG, "null frame buffer");
        return;
    }

    if (len < 2) {
        ESP_LOGW(TAG, "frame too short: len=%u", (unsigned)len);
        return;
    }

    uint16_t fc = read_le16(&buf[0]);

    uint8_t version = fc & 0x3;
    uint8_t type = (fc >> 2) & 0x3;
    uint8_t subtype = (fc >> 4) & 0xf;

    bool to_ds = (fc & (1 << 8)) != 0;
    bool from_ds = (fc & (1 << 9)) != 0;
    bool retry = (fc & (1 << 11)) != 0;
    bool protected_frame = (fc & (1 << 14)) != 0;

    ESP_LOGI(TAG,
             "RX ver=%u type=%s subtype=%s raw_subtype=%u len=%u to_ds=%u from_ds=%u "
             "retry=%u protected=%u",
             version,
             frame_type_to_string(type),
             frame_subtype_to_string(type, subtype),
             subtype,
             (unsigned)len,
             to_ds ? 1 : 0,
             from_ds ? 1 : 0,
             retry ? 1 : 0,
             protected_frame ? 1 : 0);

    if (len >= 24 && (type == 0 || type == 2)) {
        char addr1[18];
        char addr2[18];
        char addr3[18];

        format_mac(addr1, &buf[4]);
        format_mac(addr2, &buf[10]);
        format_mac(addr3, &buf[16]);

        ESP_LOGI(TAG, "addr1=%s addr2=%s addr3=%s", addr1, addr2, addr3);
    }
}
