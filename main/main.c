#include "esp_log.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pf.h"

static const char *TAG = "app";
static const uint8_t TEST_CHANNEL = 0;

static bool is_interesting_packet(const pf_packet_t *packet)
{
    if (packet == NULL || packet->type != PF_PACKET_TYPE_MANAGEMENT) {
        return false;
    }

    return strcmp(packet->subtype_name, "Beacon") == 0 ||
           strcmp(packet->subtype_name, "ProbeRequest") == 0 ||
           strcmp(packet->subtype_name, "ProbeResponse") == 0;
}

static void log_interesting_packets(uint32_t *last_seen_id)
{
    const pf_packet_list_t *packets = pf_sniff(TEST_CHANNEL);

    for (size_t i = 0; i < packets->count; i++) {
        const pf_packet_t *packet = pf_packet_list_get(packets, i);

        if (packet == NULL || packet->id <= *last_seen_id) {
            continue;
        }

        if (is_interesting_packet(packet)) {
            ESP_LOGI(TAG,
                     "%s ssid=\"%s\" len=%lu offset=%lu rssi=%d channel=%u secondary=%u",
                     packet->subtype_name,
                     packet->has_ssid ? packet->ssid : "<hidden>",
                     packet->frame_len,
                     packet->frame_offset,
                     packet->rssi,
                     packet->has_channel ? packet->primary_channel : 0,
                     packet->has_channel ? packet->secondary_channel : 0);
        }

        *last_seen_id = packet->id;
    }
}

static void log_channel_config_if_changed(void)
{
    static bool first = true;
    static pf_channel_config_t last_cfg;
    pf_channel_config_t cfg = pf_get_channel_config();

    if (!first &&
        cfg.configured == last_cfg.configured &&
        cfg.backend_applied == last_cfg.backend_applied &&
        cfg.primary == last_cfg.primary &&
        cfg.secondary == last_cfg.secondary) {
        return;
    }

    first = false;
    last_cfg = cfg;

    ESP_LOGI(TAG,
             "sniff mode: %s configured=%u backend_applied=%u primary=%u secondary=%u",
             TEST_CHANNEL == 0 ? "hopping" : "fixed",
             cfg.configured ? 1 : 0,
             cfg.backend_applied ? 1 : 0,
             cfg.primary,
             cfg.secondary);
}

void app_main(void)
{
    if (!pf_init()) {
        ESP_LOGE(TAG, "pf_init failed");
    }

    uint32_t last_seen_id = 0;

    while (1) {
        log_interesting_packets(&last_seen_id);
        log_channel_config_if_changed();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
