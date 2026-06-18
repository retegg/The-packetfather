#include "pf.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "wifi_dma.h"
#include "wifi_power.h"

static const char *TAG = "pf";

static bool s_initialized;
static bool s_sniffing;
static bool s_debug_enabled;
static pf_capture_mode_t s_capture_mode = PF_CAPTURE_MODE_SMART;
static pf_capture_selector_t s_capture_selector;
static void *s_capture_selector_ctx;

bool pf_debug_enabled(void)
{
    return s_debug_enabled;
}

void pf_debug(bool enabled)
{
    s_debug_enabled = enabled;
}

void pf_set_capture_mode(pf_capture_mode_t mode)
{
    s_capture_mode = mode;
}

pf_capture_mode_t pf_get_capture_mode(void)
{
    return s_capture_mode;
}

void pf_set_capture_selector(pf_capture_selector_t selector, void *ctx)
{
    s_capture_selector = selector;
    s_capture_selector_ctx = ctx;
}

bool pf_should_capture_raw(const pf_packet_t *packet)
{
    if (packet == NULL) {
        return false;
    }

    if (s_capture_mode == PF_CAPTURE_MODE_FULL) {
        return true;
    }

    if (s_capture_mode != PF_CAPTURE_MODE_SMART || s_capture_selector == NULL) {
        return false;
    }

    return s_capture_selector(packet, s_capture_selector_ctx);
}

bool pf_init(void)
{
    if (s_initialized) {
        if (pf_debug_enabled()) {
            ESP_LOGW(TAG, "pf_init called more than once");
        }

        return true;
    }

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "initializing packet stack");
    }

    pf_packet_history_clear();

    s_initialized = true;

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "packet stack initialized");
    }

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

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "starting sniffer");
    }

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

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "sniffer started");
    }

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

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "stopping sniffer");
    }

    wifi_dma_rx_stop_polling();
    wifi_power_disable_minimal();

    s_sniffing = false;

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "sniffer stopped");
    }
}

void pf_clear_packets(void)
{
    pf_packet_history_clear();
}
