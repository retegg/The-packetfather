/*
 * Lightweight Wi-Fi register probe helpers.
 *
 * These functions only read observed MMIO registers. They are useful while
 * comparing traces between firmware versions or hardware experiments.
 */

#include "wifi_probe.h"
#include "wifi_regs.h"

#include <stdint.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_probe";

void wifi_probe_dump_registers(void)
{
    ESP_LOGI(TAG, "---- Wi-Fi register probe ----");
    ESP_LOGI(TAG, "target: %s", WIFI_TARGET_NAME);

    ESP_LOGI(TAG, "WIFI_BASE_RX_DSCR   = 0x%08lx", WIFI_BASE_RX_DSCR);
    ESP_LOGI(TAG, "WIFI_NEXT_RX_DSCR   = 0x%08lx", WIFI_NEXT_RX_DSCR);
    ESP_LOGI(TAG, "WIFI_LAST_RX_DSCR   = 0x%08lx", WIFI_LAST_RX_DSCR);
    ESP_LOGI(TAG, "WIFI_DMA_INT_STATUS = 0x%08lx", WIFI_DMA_INT_STATUS);
    ESP_LOGI(TAG, "WIFI_MAC_BITMASK_084= 0x%08lx", WIFI_MAC_BITMASK_084);
    ESP_LOGI(TAG, "WIFI_MAC_CTRL_REG   = 0x%08lx", WIFI_MAC_CTRL_REG);

    ESP_LOGI(TAG, "------------------------------");
}

void wifi_probe_poll_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Wi-Fi probe poll task started");

    uint32_t last_status = 0xffffffff;

    while (1) {
        uint32_t status = WIFI_DMA_INT_STATUS;

        if (status != last_status) {
            ESP_LOGI(TAG, "INT_STATUS changed: 0x%08lx -> 0x%08lx", last_status, status);
            last_status = status;
        }

        if (status != 0) {
            ESP_LOGW(TAG, "INT_STATUS active: 0x%08lx", status);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
