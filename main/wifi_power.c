/*
 * Low-level Wi-Fi/RF power bring-up for ESP32-C3.
 *
 * This intentionally does not start the vendor Wi-Fi stack, join a network, or create
 * sockets. It only prepares the hardware domain so experimental MMIO access can
 * be observed.
 */

#include "wifi_power.h"

#include "pf.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_phy_init.h"
#include "esp_private/periph_ctrl.h"
#include "nvs_flash.h"
#include "soc/periph_defs.h"

static const char *TAG = "wifi_power";

#define PF_DEBUG_LOGI(...)            \
    do {                              \
        if (pf_debug_enabled()) {     \
            ESP_LOGI(__VA_ARGS__);    \
        }                             \
    } while (0)

#define PF_DEBUG_LOGW(...)            \
    do {                              \
        if (pf_debug_enabled()) {     \
            ESP_LOGW(__VA_ARGS__);    \
        }                             \
    } while (0)

/*
 * Provided by ESP-IDF/esp_phy. Some IDF versions do not expose a public
 * prototype, so this local declaration keeps the call explicit.
 */
void esp_wifi_power_domain_on(void);

static void wifi_power_init_nvs_for_phy(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        PF_DEBUG_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
    PF_DEBUG_LOGI(TAG, "NVS ready for PHY calibration");
}

void wifi_power_enable_minimal(void)
{
    PF_DEBUG_LOGI(TAG, "Wi-Fi low-level power enable start");

    wifi_power_init_nvs_for_phy();

    PF_DEBUG_LOGI(TAG, "calling RF power domain on");
    esp_wifi_power_domain_on();

    PF_DEBUG_LOGI(TAG, "calling esp_phy_common_clock_enable()");
    esp_phy_common_clock_enable();

    PF_DEBUG_LOGI(TAG, "calling esp_phy_enable()");
    esp_phy_enable();

    PF_DEBUG_LOGI(TAG, "calling periph_module_enable(PERIPH_WIFI_MODULE)");
    periph_module_enable(PERIPH_WIFI_MODULE);

    /*
     * Reset before programming DMA/MAC registers. A later reset would clear
     * the experimental register state configured by wifi_dma.
     */
    PF_DEBUG_LOGI(TAG, "calling periph_module_reset(PERIPH_WIFI_MODULE)");
    periph_module_reset(PERIPH_WIFI_MODULE);

    PF_DEBUG_LOGI(TAG, "Wi-Fi low-level power enable done");
}

void wifi_power_disable_minimal(void)
{
    PF_DEBUG_LOGI(TAG, "Wi-Fi low-level power disable start");

    periph_module_reset(PERIPH_WIFI_MODULE);
    periph_module_disable(PERIPH_WIFI_MODULE);
    esp_phy_disable();
    esp_phy_common_clock_disable();

    PF_DEBUG_LOGI(TAG, "Wi-Fi low-level power disable done");
}
