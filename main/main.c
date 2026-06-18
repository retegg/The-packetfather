/*
 * The Packetfather startup sequence.
 *
 * Keep app_main small: power the Wi-Fi hardware, prepare RX DMA, attach the
 * descriptor ring, then start the polling task used by the current experiment.
 */

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_dma.h"
#include "wifi_power.h"
#include "wifi_regdump.h"

static const char *TAG = "packetfather";

void app_main(void)
{
    ESP_LOGI(TAG, "The Packetfather starts");

    /* Power and PHY setup must happen before touching Wi-Fi MMIO registers. */
    wifi_power_enable_minimal();

    /* Build the circular RX DMA descriptor chain in internal DMA-capable RAM. */
    wifi_dma_rx_setup();

    /* Publish the descriptor chain to the observed Wi-Fi RX DMA registers. */
    wifi_dma_rx_attach_to_hardware();

    /* Experimental nudge for the RX hardware to consume the descriptor chain. */
    wifi_dma_rx_kick_hardware();

    wifi_dma_dump_registers("after rx attach");

    xTaskCreate(wifi_dma_rx_poll_task, "rx_poll", 4096, NULL, 6, NULL);

    /*
     * Optional research task. It only reads register ranges and is intentionally
     * disabled during normal RX experiments to keep logs and heat down.
     */
    /*
    xTaskCreate(wifi_regdump_task, "regdump", 4096, NULL, 3, NULL);
    */

    ESP_LOGI(TAG, "The Packetfather init finished");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
