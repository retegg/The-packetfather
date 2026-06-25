#include "esp_log.h"
#include "esp_mac.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pf.h"
#include "tx.h"

static const char *TAG = "app";
static const char *TEST_TX_SSID = "HELLO WORLD";
static const uint8_t TEST_TX_CHANNEL = 6;
static const uint32_t TEST_TX_PERIOD_MS = 100;
static const uint8_t TEST_PROBE_SOURCE_MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

#define IEEE80211_FC_PROBE_REQUEST 0x0040
#define IEEE80211_FC_BEACON 0x0080
#define IEEE80211_MGMT_HEADER_LEN 24

static void write_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xff);
    out[1] = (uint8_t)(value >> 8);
}

static void write_le64(uint8_t *out, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(value >> (i * 8));
    }
}

static bool append_ie(uint8_t *frame, uint32_t *offset, uint8_t id, const uint8_t *data, uint8_t len)
{
    if (frame == NULL || offset == NULL || data == NULL || (*offset + 2 + len) > PF_TX_MAX_FRAME_LEN) {
        return false;
    }

    frame[(*offset)++] = id;
    frame[(*offset)++] = len;
    memcpy(frame + *offset, data, len);
    *offset += len;
    return true;
}

static bool append_common_probe_ies(uint8_t *frame, uint32_t *offset)
{
    static const uint8_t supported_rates[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
    uint8_t channel = TEST_TX_CHANNEL;
    size_t ssid_len = strlen(TEST_TX_SSID);

    if (ssid_len > 32) {
        ssid_len = 32;
    }

    return append_ie(frame, offset, 0, (const uint8_t *)TEST_TX_SSID, (uint8_t)ssid_len) &&
           append_ie(frame, offset, 1, supported_rates, sizeof(supported_rates)) &&
           append_ie(frame, offset, 3, &channel, sizeof(channel));
}

static bool build_test_beacon(uint8_t *frame, uint32_t *out_len, uint8_t out_bssid[6])
{
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t supported_rates[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
    static const uint8_t country_es[] = {'E', 'S', ' ', 1, 13, 20};
    uint8_t bssid[6];
    uint8_t channel = TEST_TX_CHANNEL;
    uint32_t offset = IEEE80211_MGMT_HEADER_LEN;
    size_t ssid_len;

    if (frame == NULL || out_len == NULL || out_bssid == NULL || channel < 1 || channel > 13) {
        return false;
    }

    if (esp_read_mac(bssid, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(TAG, "failed to read local Wi-Fi MAC");
        return false;
    }

    bssid[0] |= 0x02;
    bssid[0] &= 0xfe;
    memcpy(out_bssid, bssid, 6);

    memset(frame, 0, PF_TX_MAX_FRAME_LEN);
    write_le16(frame + 0, IEEE80211_FC_BEACON);
    memcpy(frame + 4, broadcast, sizeof(broadcast));
    memcpy(frame + 10, bssid, sizeof(bssid));
    memcpy(frame + 16, bssid, sizeof(bssid));

    write_le64(frame + offset, 0);
    offset += 8;
    write_le16(frame + offset, 100);
    offset += 2;
    write_le16(frame + offset, 0x0001);
    offset += 2;

    ssid_len = strlen(TEST_TX_SSID);
    if (ssid_len > 32) {
        ssid_len = 32;
    }

    if (!append_ie(frame, &offset, 0, (const uint8_t *)TEST_TX_SSID, (uint8_t)ssid_len) ||
        !append_ie(frame, &offset, 1, supported_rates, sizeof(supported_rates)) ||
        !append_ie(frame, &offset, 3, &channel, sizeof(channel)) ||
        !append_ie(frame, &offset, 7, country_es, sizeof(country_es))) {
        return false;
    }

    *out_len = offset;
    return true;
}

static bool build_test_probe_request(uint8_t *frame, uint32_t *out_len)
{
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint32_t offset = IEEE80211_MGMT_HEADER_LEN;

    if (frame == NULL || out_len == NULL) {
        return false;
    }

    memset(frame, 0, PF_TX_MAX_FRAME_LEN);
    write_le16(frame + 0, IEEE80211_FC_PROBE_REQUEST);
    memcpy(frame + 4, broadcast, sizeof(broadcast));
    memcpy(frame + 10, TEST_PROBE_SOURCE_MAC, sizeof(TEST_PROBE_SOURCE_MAC));
    memcpy(frame + 16, broadcast, sizeof(broadcast));

    if (!append_common_probe_ies(frame, &offset)) {
        return false;
    }

    *out_len = offset;
    return true;
}

void app_main(void)
{
    uint8_t beacon[PF_TX_MAX_FRAME_LEN];
    uint8_t probe_request[PF_TX_MAX_FRAME_LEN];
    uint32_t beacon_len;
    uint32_t probe_request_len;
    uint16_t sequence = 0;
    uint8_t tx_bssid[6];

    if (!pf_init()) {
        ESP_LOGE(TAG, "pf_init failed");
        return;
    }

    pf_debug(false);

    if (!pf_set_channel(TEST_TX_CHANNEL, PF_SECONDARY_CHANNEL_NONE)) {
        ESP_LOGE(TAG, "failed to configure test channel=%u", (unsigned)TEST_TX_CHANNEL);
        return;
    }

    if (!build_test_beacon(beacon, &beacon_len, tx_bssid)) {
        ESP_LOGE(TAG, "failed to build HELLO WORLD beacon");
        return;
    }

    if (!build_test_probe_request(probe_request, &probe_request_len)) {
        ESP_LOGE(TAG, "failed to build HELLO WORLD probe request");
        return;
    }

    ESP_LOGI(TAG,
             "HELLO WORLD beacon MAC=%02x:%02x:%02x:%02x:%02x:%02x probe SA=%02x:%02x:%02x:%02x:%02x:%02x channel=%u",
             tx_bssid[0], tx_bssid[1], tx_bssid[2], tx_bssid[3], tx_bssid[4], tx_bssid[5],
             TEST_PROBE_SOURCE_MAC[0], TEST_PROBE_SOURCE_MAC[1], TEST_PROBE_SOURCE_MAC[2],
             TEST_PROBE_SOURCE_MAC[3], TEST_PROBE_SOURCE_MAC[4], TEST_PROBE_SOURCE_MAC[5],
             (unsigned)TEST_TX_CHANNEL);
    ESP_LOGI(TAG,
             "wireshark_filter=\"wlan.ssid == \\\"HELLO WORLD\\\" || wlan.sa == 02:11:22:33:44:55\"");

    while (1) {
        uint16_t beacon_seq = sequence++ & 0x0fff;
        uint16_t probe_seq = sequence++ & 0x0fff;

        write_le16(beacon + 22, (uint16_t)(beacon_seq << 4));
        write_le16(probe_request + 22, (uint16_t)(probe_seq << 4));

        if (pf_tx_raw(beacon, beacon_len)) {
            if ((beacon_seq % 10) == 0) {
                ESP_LOGI(TAG,
                         "TX complete HELLO WORLD beacon seq=%u len=%lu channel=%u",
                         beacon_seq,
                         (unsigned long)beacon_len,
                         (unsigned)TEST_TX_CHANNEL);
            }
        } else {
            ESP_LOGE(TAG, "TX failed for HELLO WORLD beacon len=%lu", (unsigned long)beacon_len);
        }

        if (pf_tx_raw(probe_request, probe_request_len)) {
            if ((probe_seq % 10) == 1) {
                ESP_LOGI(TAG,
                         "TX complete HELLO WORLD probe request seq=%u sa=02:11:22:33:44:55 len=%lu channel=%u",
                         probe_seq,
                         (unsigned long)probe_request_len,
                         (unsigned)TEST_TX_CHANNEL);
            }
        } else {
            ESP_LOGE(TAG, "TX failed for HELLO WORLD probe request len=%lu", (unsigned long)probe_request_len);
        }

        vTaskDelay(pdMS_TO_TICKS(TEST_TX_PERIOD_MS));
    }
}
