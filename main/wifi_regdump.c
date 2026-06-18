#include "wifi_regdump.h"
#include "wifi_regs.h"

#include <stdint.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_regdump";

static void dump_range(uint32_t start, uint32_t end)
{
    for (uint32_t addr = start; addr <= end; addr += sizeof(uint32_t)) {
        uint32_t value = WIFI_MMIO_DWORD(addr);

        if (value != 0) {
            ESP_LOGI(TAG, "REG[0x%08lx] = 0x%08lx", addr, value);
        }
    }
}

void wifi_regdump_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Wi-Fi register dump task started");

    while (1) {
        ESP_LOGW(TAG, "---- MAC register window ----");
        dump_range(WIFI_REG_BASE_MAC, WIFI_REG_BASE_MAC + 0x100);

        ESP_LOGW(TAG, "---- DMA register window ----");
        dump_range(WIFI_REG_BASE_DMA, WIFI_REG_BASE_DMA + 0x100);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
