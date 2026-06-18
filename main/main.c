#include "esp_log.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pf.h"

static const char *TAG = "app";

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
    const pf_packet_list_t *packets = pf_sniff();

    for (size_t i = 0; i < packets->count; i++) {
        const pf_packet_t *packet = pf_packet_list_get(packets, i);

        if (packet == NULL || packet->id <= *last_seen_id) {
            continue;
        }

        if (is_interesting_packet(packet)) {
            ESP_LOGI(TAG,
                     "%s ssid=\"%s\" len=%lu offset=%lu",
                     packet->subtype_name,
                     packet->has_ssid ? packet->ssid : "<hidden>",
                     packet->frame_len,
                     packet->frame_offset);
        }

        *last_seen_id = packet->id;
    }
}

void app_main(void)
{
    if (!pf_init()) {
        ESP_LOGE(TAG, "pf_init failed");
    }

    uint32_t last_seen_id = 0;

    while (1) {
        log_interesting_packets(&last_seen_id);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
