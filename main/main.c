#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "packet_builder.h"
#include "pf.h"
#include "tx.h"

static const char *TAG = "app";
static const char *TEST_TX_SSID = "HELLO WORLD";
static const uint8_t TEST_TX_CHANNEL = 6;
static const uint32_t TEST_TX_PERIOD_MS = 100;
static const uint32_t TEST_TX_INTERFRAME_MS = 8;
static const uint8_t TEST_PROBE_SOURCE_MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};


void app_main(void)
{
    pf_frame_t beacon;
    pf_frame_t probe_request;
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

    if (!pf_get_esp_packet_mac(tx_bssid)) {
        ESP_LOGE(TAG, "failed to read local Wi-Fi packet MAC");
        return;
    }

    beacon = Beacon(TEST_TX_SSID, tx_bssid, TEST_TX_CHANNEL, NULL);
    if (!beacon.ok) {
        ESP_LOGE(TAG, "failed to build HELLO WORLD beacon");
        return;
    }

    probe_request = ProbeRequest(TEST_TX_SSID, TEST_PROBE_SOURCE_MAC, TEST_TX_CHANNEL, NULL);
    if (!probe_request.ok) {
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

        pf_frame_set_sequence(&beacon, beacon_seq);
        pf_frame_set_sequence(&probe_request, probe_seq);

        if (pf_tx_raw(beacon.bytes, beacon.len)) {
            if ((beacon_seq % 10) == 0) {
                ESP_LOGI(TAG,
                         "TX complete HELLO WORLD beacon seq=%u len=%lu channel=%u",
                         beacon_seq,
                         (unsigned long)beacon.len,
                         (unsigned)TEST_TX_CHANNEL);
            }
        } else {
            ESP_LOGE(TAG, "TX failed for HELLO WORLD beacon len=%lu", (unsigned long)beacon.len);
        }

        vTaskDelay(pdMS_TO_TICKS(TEST_TX_INTERFRAME_MS));

        if (pf_tx_raw(probe_request.bytes, probe_request.len)) {
            if ((probe_seq % 10) == 1) {
                ESP_LOGI(TAG,
                         "TX complete HELLO WORLD probe request seq=%u sa=02:11:22:33:44:55 len=%lu channel=%u",
                         probe_seq,
                         (unsigned long)probe_request.len,
                         (unsigned)TEST_TX_CHANNEL);
            }
        } else {
            ESP_LOGE(TAG, "TX failed for HELLO WORLD probe request len=%lu", (unsigned long)probe_request.len);
        }

        vTaskDelay(pdMS_TO_TICKS(TEST_TX_PERIOD_MS));
    }
}
