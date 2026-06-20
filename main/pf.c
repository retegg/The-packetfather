#include "pf.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_dma.h"
#include "wifi_power.h"

static const char *TAG = "pf";
#define PF_HOP_CHANNEL_MIN 1
#define PF_HOP_CHANNEL_MAX 13
#define PF_HOP_DWELL_MS 500
#define PF_CONNECT_SCAN_TIMEOUT_MS 5000
#define PF_CONNECT_SCAN_POLL_MS 50

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
static pf_network_list_t s_networks;
static pf_packet_metadata_t s_network_packet;
static uint32_t s_last_network_packet_id;
static pf_connection_target_t s_connection_target;

extern int chip_v7_set_chan(uint8_t primary, uint8_t secondary);

static void pf_connection_target_clear(void);
static void pf_connection_target_set(const pf_network_t *network, const char *password);

static void mac_to_text(const uint8_t mac[6], char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    snprintf(out,
             out_len,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

static const char *channel_text(const pf_network_t *network, char *out, size_t out_len)
{
    if (network == NULL || out == NULL || out_len == 0) {
        return "?";
    }

    if (!network->has_channel) {
        return "?";
    }

    snprintf(out, out_len, "%u", network->primary_channel);
    return out;
}

static void pf_update_networks_from_history(void)
{
    size_t count = pf_packet_history_count();

    for (size_t i = 0; i < count; i++) {
        if (!pf_packet_history_get_metadata(i, &s_network_packet)) {
            continue;
        }

        if (s_network_packet.id <= s_last_network_packet_id) {
            continue;
        }

        (void)pf_network_list_update_from_packet(&s_networks, &s_network_packet);
        s_last_network_packet_id = s_network_packet.id;
    }
}

static bool network_matches_ssid(const pf_network_t *network, const char *ssid)
{
    return network != NULL && ssid != NULL && network->has_ssid && strcmp(network->ssid, ssid) == 0;
}

static bool network_is_better_candidate(const pf_network_t *candidate, const pf_network_t *best)
{
    if (best == NULL) {
        return true;
    }

    if (candidate->has_rssi && !best->has_rssi) {
        return true;
    }

    if (!candidate->has_rssi || !best->has_rssi) {
        return false;
    }

    return candidate->average_rssi > best->average_rssi;
}

static bool pf_find_network_by_ssid(const char *ssid, pf_network_t *out)
{
    const pf_network_t *best = NULL;

    if (ssid == NULL || ssid[0] == '\0' || out == NULL) {
        return false;
    }

    for (size_t i = 0; i < s_networks.count; i++) {
        const pf_network_t *network = &s_networks.items[i];

        if (!network_matches_ssid(network, ssid)) {
            continue;
        }

        if (network_is_better_candidate(network, best)) {
            best = network;
        }
    }

    if (best == NULL) {
        return false;
    }

    *out = *best;
    return true;
}

static void pf_connection_target_clear(void)
{
    memset(&s_connection_target, 0, sizeof(s_connection_target));
}

static void pf_connection_target_set(const pf_network_t *network, const char *password)
{
    size_t password_len;

    pf_connection_target_clear();
    s_connection_target.configured = true;
    memcpy(s_connection_target.ssid, network->ssid, sizeof(s_connection_target.ssid));
    memcpy(s_connection_target.bssid, network->bssid, sizeof(s_connection_target.bssid));
    s_connection_target.has_channel = network->has_channel;
    s_connection_target.primary_channel = network->primary_channel;
    s_connection_target.security = network->security;
    s_connection_target.group_cipher = network->group_cipher;
    s_connection_target.pairwise_cipher = network->pairwise_cipher;
    s_connection_target.akm = network->akm;
    s_connection_target.has_rsn_capabilities = network->has_rsn_capabilities;
    s_connection_target.rsn_capabilities = network->rsn_capabilities;

    if (password == NULL || password[0] == '\0') {
        return;
    }

    password_len = strlen(password);

    if (password_len >= sizeof(s_connection_target.password)) {
        password_len = sizeof(s_connection_target.password) - 1;
    }

    memcpy(s_connection_target.password, password, password_len);
    s_connection_target.password[password_len] = '\0';
    s_connection_target.has_password = true;
}

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
    return primary >= PF_HOP_CHANNEL_MIN && primary <= PF_HOP_CHANNEL_MAX;
}

static bool pf_apply_channel_backend(uint8_t primary, pf_secondary_channel_t secondary)
{
    int ret;

    if (!pf_channel_is_valid_primary(primary) || secondary != PF_SECONDARY_CHANNEL_NONE) {
        return false;
    }

    ret = chip_v7_set_chan(primary, (uint8_t)secondary);

    if (pf_debug_enabled()) {
        ESP_LOGI(TAG, "channel backend ret=%d primary=%u secondary=%u", ret, primary, secondary);
    }

    return ret == 0;
}

static void pf_apply_pending_channel_config(void)
{
    if (!s_channel_config.configured) {
        return;
    }

    s_channel_config.backend_applied =
        pf_apply_channel_backend(s_channel_config.primary, s_channel_config.secondary);
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

    if (s_sniffing) {
        s_channel_config.backend_applied = pf_apply_channel_backend(primary, secondary);
    } else {
        s_channel_config.backend_applied = false;
    }

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
    pf_network_list_init(&s_networks);
    s_last_network_packet_id = 0;
    pf_connection_target_clear();
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
    pf_apply_pending_channel_config();
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

    if (!s_channel_config.backend_applied) {
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
    pf_update_networks_from_history();
    return pf_packet_history();
}

size_t pf_network_count(void)
{
    pf_update_networks_from_history();
    return pf_network_list_count(&s_networks);
}

bool pf_network_get_copy(size_t index, pf_network_t *out)
{
    pf_update_networks_from_history();
    return pf_network_list_get_copy(&s_networks, index, out);
}

bool pf_connect(const char *ssid, const char *password)
{
    pf_network_t network;
    char bssid[18];
    char channel[8];
    bool password_provided = password != NULL && password[0] != '\0';
    const char *auth_text;
    TickType_t start_tick;
    TickType_t timeout_ticks = pdMS_TO_TICKS(PF_CONNECT_SCAN_TIMEOUT_MS);

    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "pf_connect requires a non-empty ssid");
        return false;
    }

    start_tick = xTaskGetTickCount();

    while (1) {
        (void)pf_sniff(0);
        pf_update_networks_from_history();

        if (pf_find_network_by_ssid(ssid, &network)) {
            break;
        }

        if ((xTaskGetTickCount() - start_tick) >= timeout_ticks) {
            ESP_LOGE(TAG,
                     "ssid=\"%s\" was not found after %u ms",
                     ssid,
                     (unsigned)PF_CONNECT_SCAN_TIMEOUT_MS);
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(PF_CONNECT_SCAN_POLL_MS));
    }

    mac_to_text(network.bssid, bssid, sizeof(bssid));
    auth_text = password_provided ? "password" : "open";

    if (!password_provided && network.security != PF_NETWORK_SECURITY_OPEN) {
        auth_text = "missing-password";
    }

    ESP_LOGI(TAG,
             "connect target ssid=\"%s\" bssid=%s channel=%s security=%s akm=%s pairwise=%s group=%s auth=%s",
             ssid,
             bssid,
             channel_text(&network, channel, sizeof(channel)),
             pf_network_security_name(network.security),
             pf_rsn_akm_name(network.akm),
             pf_rsn_cipher_name(network.pairwise_cipher),
             pf_rsn_cipher_name(network.group_cipher),
             auth_text);

    if (!password_provided && network.security != PF_NETWORK_SECURITY_OPEN) {
        ESP_LOGE(TAG,
                 "ssid=\"%s\" requires a password, security=%s",
                 ssid,
                 pf_network_security_name(network.security));
        return false;
    }

    if (!network.has_channel) {
        ESP_LOGE(TAG, "ssid=\"%s\" has no discovered channel", ssid);
        return false;
    }

    if (password_provided && network.security == PF_NETWORK_SECURITY_OPEN) {
        ESP_LOGW(TAG, "ssid=\"%s\" is open; password will be ignored later", ssid);
    }

    if (!pf_set_channel(network.primary_channel, PF_SECONDARY_CHANNEL_NONE)) {
        ESP_LOGE(TAG, "failed to fix channel %u for ssid=\"%s\"", network.primary_channel, ssid);
        return false;
    }

    pf_connection_target_set(&network, password);
    ESP_LOGI(TAG, "connection target prepared on channel=%u", network.primary_channel);
    return true;
}
bool pf_get_connection_target(pf_connection_target_t *out)
{
    if (out == NULL || !s_connection_target.configured) {
        return false;
    }

    *out = s_connection_target;
    return true;
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
    pf_network_list_init(&s_networks);
    s_last_network_packet_id = 0;
    pf_connection_target_clear();
}
