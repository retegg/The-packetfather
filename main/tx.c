#include "tx.h"

#include <stddef.h>

#include "esp_log.h"

#include "wifi_tx.h"

static const char *TAG = "tx";

bool pf_tx_raw(const uint8_t *frame, uint32_t len)
{
    if (frame == NULL || len == 0 || len > PF_TX_MAX_FRAME_LEN) {
        ESP_LOGE(TAG, "invalid TX frame len=%lu", (unsigned long)len);
        return false;
    }

    return wifi_tx_raw(frame, len);
}