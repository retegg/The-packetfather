#include "tx.h"

#include <stddef.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "pf.h"

static const char *TAG = "tx";

static bool s_tx_wifi_ready;

static esp_err_t pf_tx_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

static uint8_t pf_tx_selected_channel(void)
{
    pf_channel_config_t channel = pf_get_channel_config();

    if (!channel.configured || channel.primary < 1 || channel.primary > 13) {
        return 1;
    }

    return channel.primary;
}

static esp_err_t pf_tx_wifi_start(uint8_t channel)
{
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_country_t country = {
        .cc = "ES",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    esp_err_t ret;

    if (s_tx_wifi_ready) {
        return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    pf_sniff_stop();

    ret = pf_tx_init_nvs();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_wifi_init(&init_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_country(&country);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        return ret;
    }

    s_tx_wifi_ready = true;
    return ESP_OK;
}

static bool pf_tx_backend_raw(const uint8_t *frame, uint32_t len)
{
    uint8_t channel = pf_tx_selected_channel();
    esp_err_t ret = pf_tx_wifi_start(channel);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start TX Wi-Fi backend: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_80211_tx(WIFI_IF_STA, frame, (int)len, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "raw TX failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool pf_tx_raw(const uint8_t *frame, uint32_t len)
{
    if (frame == NULL || len == 0 || len > PF_TX_MAX_FRAME_LEN) {
        ESP_LOGE(TAG, "invalid TX frame len=%lu", (unsigned long)len);
        return false;
    }

    return pf_tx_backend_raw(frame, len);
}