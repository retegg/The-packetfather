#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pf.h"
#include "tx.h"

static const char *TAG = "app";
static const char *TEST_BEACON_SSID = "TEST WIFI";
static const uint8_t TEST_BEACON_CHANNEL = 6;
static const uint32_t TEST_BEACON_TX_PERIOD_MS = 500;

#define IEEE80211_FC_BEACON 0x0080
#define IEEE80211_MGMT_HEADER_LEN 24
#define IEEE80211_CAPABILITY_ESS 0x0001
#define IEEE80211_BEACON_INTERVAL_TU 100

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

static void set_sequence_control(uint8_t *frame, uint16_t sequence)
{
    write_le16(frame + 22, (uint16_t)((sequence & 0x0fff) << 4));
}

static bool build_test_beacon(uint8_t *frame, uint32_t *out_len)
{
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t supported_rates[] = {0x82, 0x84, 0x8b, 0x96};
    static const uint8_t country_es[] = {'E', 'S', ' ', 1, 13, 20};
    uint8_t bssid[6];
    uint8_t channel = TEST_BEACON_CHANNEL;
    uint32_t offset;
    size_t ssid_len;

    if (frame == NULL || out_len == NULL || channel < 1 || channel > 13) {
        return false;
    }

    if (esp_read_mac(bssid, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(TAG, "failed to read local Wi-Fi MAC");
        return false;
    }

    bssid[0] |= 0x02;
    bssid[0] &= 0xfe;

    memset(frame, 0, PF_TX_MAX_FRAME_LEN);
    write_le16(frame + 0, IEEE80211_FC_BEACON);
    write_le16(frame + 2, 0);
    memcpy(frame + 4, broadcast, sizeof(broadcast));
    memcpy(frame + 10, bssid, sizeof(bssid));
    memcpy(frame + 16, bssid, sizeof(bssid));
    write_le16(frame + 22, 0);

    offset = IEEE80211_MGMT_HEADER_LEN;
    write_le64(frame + offset, 0);
    offset += 8;
    write_le16(frame + offset, IEEE80211_BEACON_INTERVAL_TU);
    offset += 2;
    write_le16(frame + offset, IEEE80211_CAPABILITY_ESS);
    offset += 2;

    ssid_len = strlen(TEST_BEACON_SSID);
    if (ssid_len > 32) {
        ssid_len = 32;
    }

    if (!append_ie(frame, &offset, 0, (const uint8_t *)TEST_BEACON_SSID, (uint8_t)ssid_len) ||
        !append_ie(frame, &offset, 1, supported_rates, sizeof(supported_rates)) ||
        !append_ie(frame, &offset, 3, &channel, sizeof(channel)) ||
        !append_ie(frame, &offset, 7, country_es, sizeof(country_es))) {
        return false;
    }

    *out_len = offset;
    return true;
}

void app_main(void)
{
    uint8_t beacon[PF_TX_MAX_FRAME_LEN];
    uint32_t beacon_len;
    uint16_t sequence = 0;

    if (!pf_init()) {
        ESP_LOGE(TAG, "pf_init failed");
        return;
    }

    if (!pf_set_channel(TEST_BEACON_CHANNEL, PF_SECONDARY_CHANNEL_NONE)) {
        ESP_LOGE(TAG, "failed to configure test channel=%u", TEST_BEACON_CHANNEL);
        return;
    }

    if (!build_test_beacon(beacon, &beacon_len)) {
        ESP_LOGE(TAG, "failed to build TEST WIFI beacon");
        return;
    }

    while (1) {
        set_sequence_control(beacon, sequence);

        if (pf_tx_raw(beacon, beacon_len)) {
            ESP_LOGI(TAG,
                     "sent TEST WIFI beacon seq=%u len=%lu channel=%u",
                     sequence,
                     (unsigned long)beacon_len,
                     TEST_BEACON_CHANNEL);
            sequence = (uint16_t)((sequence + 1) & 0x0fff);
        } else {
            ESP_LOGE(TAG, "pf_tx_raw failed for TEST WIFI beacon len=%lu", (unsigned long)beacon_len);
        }

        vTaskDelay(pdMS_TO_TICKS(TEST_BEACON_TX_PERIOD_MS));
    }
}
