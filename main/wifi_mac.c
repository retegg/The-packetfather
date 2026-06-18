/*
 * Experimental Wi-Fi MAC RX control.
 *
 * This module only touches local receive/filter registers. It does not transmit
 * frames, inject packets, or spoof another device.
 */

#include "wifi_mac.h"
#include "wifi_regs.h"

#include <stdint.h>

#include "esp_log.h"
#include "pf.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_mac";

#define PF_DEBUG_LOGI(...)            \
    do {                              \
        if (pf_debug_enabled()) {     \
            ESP_LOGI(__VA_ARGS__);    \
        }                             \
    } while (0)

static const uint8_t s_test_mac[6] = {0x02, 0x12, 0x34, 0x56, 0x78, 0x9a};
static const uint8_t s_broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void wifi_mac_write_addr_pair(volatile uint32_t *low_reg,
                                     volatile uint32_t *high_reg,
                                     const uint8_t addr[6])
{
    *low_reg = ((uint32_t)addr[0]) | ((uint32_t)addr[1] << 8) |
               ((uint32_t)addr[2] << 16) | ((uint32_t)addr[3] << 24);

    *high_reg = ((uint32_t)addr[4]) | ((uint32_t)addr[5] << 8);
}

void wifi_mac_dump_extra_registers(void)
{
    PF_DEBUG_LOGI(TAG, "---- MAC extra register dump ----");
    PF_DEBUG_LOGI(TAG, "WIFI_MAC_CTRL_REG     = 0x%08lx", WIFI_MAC_CTRL_REG);
    PF_DEBUG_LOGI(TAG, "WIFI_RX_POLICY_0      = 0x%08lx", WIFI_RX_POLICY_0);
    PF_DEBUG_LOGI(TAG, "WIFI_RX_POLICY_1      = 0x%08lx", WIFI_RX_POLICY_1);
    PF_DEBUG_LOGI(TAG, "WIFI_MAC_ADDR0_LOW    = 0x%08lx", WIFI_MAC_ADDR0_LOW);
    PF_DEBUG_LOGI(TAG, "WIFI_MAC_ADDR0_HIGH   = 0x%08lx", WIFI_MAC_ADDR0_HIGH);
    PF_DEBUG_LOGI(TAG, "WIFI_MAC_ADDR0_CTRL   = 0x%08lx", WIFI_MAC_ADDR0_CTRL);
    PF_DEBUG_LOGI(TAG, "WIFI_BSSID0_LOW       = 0x%08lx", WIFI_BSSID0_LOW);
    PF_DEBUG_LOGI(TAG, "WIFI_BSSID0_HIGH      = 0x%08lx", WIFI_BSSID0_HIGH);
    PF_DEBUG_LOGI(TAG, "WIFI_BSSID0_MASK_LOW  = 0x%08lx", WIFI_BSSID0_MASK_LOW);
    PF_DEBUG_LOGI(TAG, "WIFI_BSSID0_MASK_HIGH = 0x%08lx", WIFI_BSSID0_MASK_HIGH);
    PF_DEBUG_LOGI(TAG, "---------------------------------");
}

void wifi_mac_rx_enable_experimental(void)
{
    PF_DEBUG_LOGI(TAG, "experimental MAC RX enable start");

    wifi_mac_dump_extra_registers();

    /*
     * OpenMAC for ESP32 classic cleared a similar control mask. On ESP32-C3 this
     * is still experimental, so keep the operation logged and easy to revert.
     */
    uint32_t mac_ctrl_before = WIFI_MAC_CTRL_REG;
    WIFI_MAC_CTRL_REG = mac_ctrl_before & 0xffffe800;
    uint32_t mac_ctrl_after = WIFI_MAC_CTRL_REG;

    PF_DEBUG_LOGI(TAG, "MAC_CTRL: before=0x%08lx after=0x%08lx", mac_ctrl_before, mac_ctrl_after);

    wifi_mac_write_addr_pair((volatile uint32_t *)&WIFI_MAC_ADDR0_LOW,
                             (volatile uint32_t *)&WIFI_MAC_ADDR0_HIGH,
                             s_test_mac);

    WIFI_MAC_ADDR0_CTRL = WIFI_MAC_ADDR0_CTRL | 0x0001ffff;

    wifi_mac_write_addr_pair((volatile uint32_t *)&WIFI_BSSID0_LOW,
                             (volatile uint32_t *)&WIFI_BSSID0_HIGH,
                             s_broadcast_mac);

    WIFI_BSSID0_MASK_LOW = 0x00000000;
    WIFI_BSSID0_MASK_HIGH = 0x00010000;

    uint32_t rx_policy_before = WIFI_RX_POLICY_0;
    WIFI_RX_POLICY_0 = rx_policy_before | 0x00000110;
    uint32_t rx_policy_after = WIFI_RX_POLICY_0;

    PF_DEBUG_LOGI(TAG,
                  "RX_POLICY_0: before=0x%08lx after=0x%08lx",
                  rx_policy_before,
                  rx_policy_after);

    wifi_mac_dump_extra_registers();

    PF_DEBUG_LOGI(TAG, "experimental MAC RX enable done");
}

void wifi_mac_rx_bit_probe_task(void *arg)
{
    (void)arg;

    PF_DEBUG_LOGI(TAG, "RX bit probe task started");

    uint32_t original_policy0 = WIFI_RX_POLICY_0;
    uint32_t original_policy1 = WIFI_RX_POLICY_1;
    uint32_t original_ctrl = WIFI_MAC_CTRL_REG;

    PF_DEBUG_LOGI(TAG, "original POLICY0=0x%08lx", original_policy0);
    PF_DEBUG_LOGI(TAG, "original POLICY1=0x%08lx", original_policy1);
    PF_DEBUG_LOGI(TAG, "original CTRL   =0x%08lx", original_ctrl);

    for (int bit = 0; bit < 32; bit++) {
        uint32_t mask = 1UL << bit;

        PF_DEBUG_LOGI(TAG, "PROBE POLICY0 bit %d mask=0x%08lx", bit, mask);
        WIFI_RX_POLICY_0 = original_policy0 | mask;
        vTaskDelay(pdMS_TO_TICKS(300));

        PF_DEBUG_LOGI(TAG,
                      "after POLICY0 bit %d: POLICY0=0x%08lx INT=0x%08lx BASE=0x%08lx "
                      "NEXT=0x%08lx LAST=0x%08lx",
                      bit,
                      WIFI_RX_POLICY_0,
                      WIFI_DMA_INT_STATUS,
                      WIFI_BASE_RX_DSCR,
                      WIFI_NEXT_RX_DSCR,
                      WIFI_LAST_RX_DSCR);

        WIFI_RX_POLICY_0 = original_policy0;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    for (int bit = 0; bit < 32; bit++) {
        uint32_t mask = 1UL << bit;

        PF_DEBUG_LOGI(TAG, "PROBE POLICY1 bit %d mask=0x%08lx", bit, mask);
        WIFI_RX_POLICY_1 = original_policy1 | mask;
        vTaskDelay(pdMS_TO_TICKS(300));

        PF_DEBUG_LOGI(TAG,
                      "after POLICY1 bit %d: POLICY1=0x%08lx INT=0x%08lx BASE=0x%08lx "
                      "NEXT=0x%08lx LAST=0x%08lx",
                      bit,
                      WIFI_RX_POLICY_1,
                      WIFI_DMA_INT_STATUS,
                      WIFI_BASE_RX_DSCR,
                      WIFI_NEXT_RX_DSCR,
                      WIFI_LAST_RX_DSCR);

        WIFI_RX_POLICY_1 = original_policy1;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    for (int bit = 0; bit < 16; bit++) {
        uint32_t mask = 1UL << bit;

        PF_DEBUG_LOGI(TAG, "PROBE CTRL clear bit %d mask=0x%08lx", bit, mask);
        WIFI_MAC_CTRL_REG = original_ctrl & ~mask;
        vTaskDelay(pdMS_TO_TICKS(300));

        PF_DEBUG_LOGI(TAG,
                      "after CTRL clear bit %d: CTRL=0x%08lx INT=0x%08lx BASE=0x%08lx "
                      "NEXT=0x%08lx LAST=0x%08lx",
                      bit,
                      WIFI_MAC_CTRL_REG,
                      WIFI_DMA_INT_STATUS,
                      WIFI_BASE_RX_DSCR,
                      WIFI_NEXT_RX_DSCR,
                      WIFI_LAST_RX_DSCR);

        WIFI_MAC_CTRL_REG = original_ctrl;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    PF_DEBUG_LOGI(TAG, "RX bit probe finished");
    vTaskDelete(NULL);
}
