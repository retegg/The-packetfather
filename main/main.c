#include "esp_log.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pf.h"

static const char *TAG = "app";
static const uint8_t TEST_CHANNEL = 0;
static pf_packet_metadata_t s_packet_metadata;

static const char *packet_rssi_text(const pf_packet_metadata_t *packet)
{
    static char text[8];

    if (packet == NULL || !packet->has_rssi) {
        return "n/a";
    }

    snprintf(text, sizeof(text), "%d", packet->rssi);
    return text;
}

static const char *packet_channel_text(const pf_packet_metadata_t *packet)
{
    static char text[8];

    if (packet == NULL || !packet->has_channel) {
        return "n/a";
    }

    snprintf(text, sizeof(text), "%u", packet->primary_channel);
    return text;
}

static bool is_interesting_packet(const pf_packet_metadata_t *packet)
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
    size_t count;

    (void)pf_sniff(TEST_CHANNEL);
    count = pf_packet_history_count();

    for (size_t i = 0; i < count; i++) {
        const pf_packet_metadata_t *packet;

        if (!pf_packet_history_get_metadata(i, &s_packet_metadata)) {
            continue;
        }

        packet = &s_packet_metadata;

        if (packet == NULL || packet->id <= *last_seen_id) {
            continue;
        }

        if (is_interesting_packet(packet)) {
            ESP_LOGI(TAG,
                     "%s ssid=\"%s\" len=%lu offset=%lu rssi=%s channel=%s",
                     packet->subtype_name,
                     packet->has_ssid ? packet->ssid : "<hidden>",
                     packet->frame_len,
                     packet->frame_offset,
                     packet_rssi_text(packet),
                     packet_channel_text(packet));
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

    if (!pf_debug_enabled()) {
        return;
    }

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
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
