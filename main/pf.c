#include "pf.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_dma.h"
#include "wifi_power.h"

static const char *TAG = "pf";
#define PF_HOP_CHANNEL_MIN 1
#define PF_HOP_CHANNEL_MAX 13
#define PF_HOP_DWELL_MS 400

static bool s_initialized;
static bool s_sniffing;
static bool s_debug_enabled;
static pf_capture_mode_t s_capture_mode = PF_CAPTURE_MODE_SMART;
static pf_capture_selector_t s_capture_selector;
static void *s_capture_selector_ctx;
static pf_channel_config_t s_channel_config;
static bool s_hopping_enabled = true;
static uint8_t s_current_hop_channel = PF_HOP_CHANNEL_MIN;
static TickType_t s_last_hop_tick;

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

static bool pf_channel_is_valid_primary(uint8_t primary)
{
    return primary >= 1 && primary <= 14;
}

static bool pf_apply_channel_backend(uint8_t primary, pf_secondary_channel_t secondary)
{
    (void)primary;
    (void)secondary;
    return false;
}

bool pf_set_channel(uint8_t primary, pf_secondary_channel_t secondary)
{
    if (!pf_channel_is_valid_primary(primary)) {
        return false;
    }

    if (secondary != PF_SECONDARY_CHANNEL_NONE) {
        return false;
    }

    s_channel_config.configured = true;
    s_channel_config.primary = primary;
    s_channel_config.secondary = secondary;
    s_channel_config.backend_applied = pf_apply_channel_backend(primary, secondary);
    return true;
}

pf_channel_config_t pf_get_channel_config(void)
{
    return s_channel_config;
}

bool pf_channel_matches_observed(uint8_t primary, pf_secondary_channel_t secondary)
{
    if (!s_channel_config.configured) {
        return true;
    }

    if (!s_channel_config.backend_applied) {
        return true;
    }

    if (primary == 0) {
        return false;
    }

    return s_channel_config.primary == primary && s_channel_config.secondary == secondary;
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
    s_hopping_enabled = true;
    s_current_hop_channel = PF_HOP_CHANNEL_MIN;
    s_last_hop_tick = 0;

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

static void pf_hop_to_next_channel_if_needed(void)
{
    TickType_t now;

    if (!s_hopping_enabled) {
        return;
    }

    now = xTaskGetTickCount();

    if (s_last_hop_tick != 0 && (now - s_last_hop_tick) < pdMS_TO_TICKS(PF_HOP_DWELL_MS)) {
        return;
    }

    if (!pf_set_channel(s_current_hop_channel, PF_SECONDARY_CHANNEL_NONE)) {
        return;
    }

    s_last_hop_tick = now;
    s_current_hop_channel++;

    if (s_current_hop_channel > PF_HOP_CHANNEL_MAX) {
        s_current_hop_channel = PF_HOP_CHANNEL_MIN;
    }
}

const pf_packet_list_t *pf_sniff(uint8_t primary_channel)
{
    if (primary_channel == 0) {
        s_hopping_enabled = true;
    } else {
        s_hopping_enabled = false;
        if (!pf_set_channel(primary_channel, PF_SECONDARY_CHANNEL_NONE)) {
            return pf_packet_history();
        }
    }

    if (!pf_start_sniffing()) {
        return pf_packet_history();
    }

    pf_hop_to_next_channel_if_needed();
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
