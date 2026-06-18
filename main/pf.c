#include "pf.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "wifi_dma.h"
#include "wifi_power.h"

static const char *TAG = "pf";

static bool s_initialized;
static bool s_sniffing;

bool pf_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "pf_init called more than once");
        return true;
    }

    ESP_LOGI(TAG, "initializing packet stack");

    pf_packet_history_clear();

    s_initialized = true;
    ESP_LOGI(TAG, "packet stack initialized");
    return true;
}

static bool pf_start_sniffing(void)
{
    if (s_sniffing) {
        return true;
    }

    if (!s_initialized && !pf_init()) {
        return false;
    }

    ESP_LOGI(TAG, "starting sniffer");

    wifi_power_enable_minimal();
    wifi_dma_rx_setup();
    wifi_dma_rx_attach_to_hardware();
    wifi_dma_rx_kick_hardware();
    wifi_dma_dump_registers("after rx attach");

    if (!wifi_dma_rx_start_polling()) {
        wifi_power_disable_minimal();
        return false;
    }

    s_sniffing = true;
    ESP_LOGI(TAG, "sniffer started");
    return true;
}

const pf_packet_list_t *pf_sniff(void)
{
    if (!pf_start_sniffing()) {
        return pf_packet_history();
    }

    return pf_packet_history();
}

void pf_sniff_stop(void)
{
    if (!s_sniffing) {
        return;
    }

    ESP_LOGI(TAG, "stopping sniffer");

    wifi_dma_rx_stop_polling();
    wifi_power_disable_minimal();

    s_sniffing = false;
    ESP_LOGI(TAG, "sniffer stopped");
}

void pf_clear_packets(void)
{
    pf_packet_history_clear();
}
