#include "wifi_dma.h"
#include "packet.h"
#include "wifi_regs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_dma";

#define WIFI_DMA_HW_ADDR_MASK 0x000fffff
#define WIFI_DMA_MAX_ALLOC_ATTEMPTS 8
#define WIFI_DMA_PADDING_MAX_BLOCKS 8
static wifi_dma_desc_t *s_rx_desc[WIFI_RX_BUFFER_COUNT];
static wifi_dma_desc_t *s_rx_chain_begin;
static wifi_dma_desc_t *s_rx_chain_last;
static TaskHandle_t s_rx_poll_task;

static void *s_dma_padding[WIFI_DMA_PADDING_MAX_BLOCKS];
static int s_dma_padding_count;

static uint32_t ptr_to_wifi_hw_addr(const void *ptr)
{
    return ((uint32_t)ptr) & WIFI_DMA_HW_ADDR_MASK;
}

static void *wifi_dma_calloc_raw(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

static void keep_padding_or_abort(void *ptr)
{
    if (ptr == NULL) {
        ESP_LOGE(TAG, "cannot keep NULL padding block");
        abort();
    }

    if (s_dma_padding_count >= WIFI_DMA_PADDING_MAX_BLOCKS) {
        ESP_LOGE(TAG, "DMA padding table full");
        abort();
    }

    s_dma_padding[s_dma_padding_count++] = ptr;
}

static bool wifi_dma_ptr_looks_reachable(const void *ptr, size_t size)
{
    uint32_t hw_start = ptr_to_wifi_hw_addr(ptr);
    uint32_t hw_end = hw_start + size - 1;
    uint32_t hw_low = WIFI_DMA_HW_LOW_REG;
    uint32_t hw_mask = WIFI_DMA_ADDR_MASK_REG;

    if (hw_low == 0 || hw_mask == 0) {
        return true;
    }

    return hw_start >= hw_low && hw_end <= hw_mask;
}

static void wifi_dma_push_heap_into_window(void)
{
    uint32_t hw_low = WIFI_DMA_HW_LOW_REG;
    uint32_t hw_mask = WIFI_DMA_ADDR_MASK_REG;

    ESP_LOGI(TAG, "DMA window low=0x%08lx mask=0x%08lx", hw_low, hw_mask);

    if (hw_low == 0 || hw_mask == 0) {
        ESP_LOGW(TAG, "DMA window registers are not ready, skipping heap push");
        return;
    }

    void *probe = wifi_dma_calloc_raw(16);

    if (probe == NULL) {
        ESP_LOGE(TAG, "failed to allocate DMA probe block");
        abort();
    }

    uint32_t probe_hw = ptr_to_wifi_hw_addr(probe);

    ESP_LOGI(TAG, "DMA probe block: ptr=%p hw=0x%08lx", probe, probe_hw);
    keep_padding_or_abort(probe);

    if (probe_hw >= hw_low && probe_hw <= hw_mask) {
        ESP_LOGI(TAG, "DMA heap already inside reachable window");
        return;
    }

    if (probe_hw < hw_low) {
        uint32_t padding_size = (hw_low - probe_hw) + 4096;
        void *padding = wifi_dma_calloc_raw(padding_size);

        if (padding == NULL) {
            ESP_LOGE(TAG, "failed to allocate DMA padding block, size=%lu", padding_size);
            abort();
        }

        ESP_LOGW(TAG,
                 "DMA padding kept: ptr=%p hw=0x%08lx size=%lu",
                 padding,
                 ptr_to_wifi_hw_addr(padding),
                 padding_size);

        keep_padding_or_abort(padding);
    }
}

static void *wifi_dma_calloc_reachable(size_t size)
{
    for (int attempt = 0; attempt < WIFI_DMA_MAX_ALLOC_ATTEMPTS; attempt++) {
        void *ptr = wifi_dma_calloc_raw(size);

        if (ptr == NULL) {
            ESP_LOGE(TAG, "DMA calloc failed, size=%u", (unsigned)size);
            return NULL;
        }

        if (wifi_dma_ptr_looks_reachable(ptr, size)) {
            ESP_LOGI(TAG,
                     "DMA alloc reachable: ptr=%p hw=0x%08lx size=%u",
                     ptr,
                     ptr_to_wifi_hw_addr(ptr),
                     (unsigned)size);
            return ptr;
        }

        ESP_LOGW(TAG,
                 "DMA alloc outside window, keeping as padding: ptr=%p hw=0x%08lx size=%u",
                 ptr,
                 ptr_to_wifi_hw_addr(ptr),
                 (unsigned)size);
        keep_padding_or_abort(ptr);
    }

    ESP_LOGE(TAG, "could not allocate reachable DMA memory");
    return NULL;
}

static void log_desc(int index, const wifi_dma_desc_t *desc)
{
    ESP_LOGI(TAG,
             "desc[%d] addr=%p hw=0x%08lx size=%u length=%u has_data=%u owner=%u "
             "packet=%p packet_hw=0x%08lx next=%p next_hw=0x%08lx",
             index,
             desc,
             ptr_to_wifi_hw_addr(desc),
             desc->size,
             desc->length,
             desc->has_data,
             desc->owner,
             desc->packet,
             ptr_to_wifi_hw_addr(desc->packet),
             desc->next,
             desc->next ? ptr_to_wifi_hw_addr(desc->next) : 0);
}

static void dump_desc_raw(int index, const wifi_dma_desc_t *desc)
{
    const volatile uint32_t *raw = (const volatile uint32_t *)desc;

    ESP_LOGW(TAG,
             "RAW desc[%d] addr=%p hw=0x%08lx word0=0x%08lx word1=0x%08lx word2=0x%08lx",
             index,
             desc,
             ptr_to_wifi_hw_addr(desc),
             raw[0],
             raw[1],
             raw[2]);

    ESP_LOGW(TAG,
             "FIELDS desc[%d] size=%u length=%u unknown=%u has_data=%u owner=%u "
             "packet=%p packet_hw=0x%08lx next=%p next_hw=0x%08lx",
             index,
             desc->size,
             desc->length,
             desc->unknown,
             desc->has_data,
             desc->owner,
             desc->packet,
             ptr_to_wifi_hw_addr(desc->packet),
             desc->next,
             desc->next ? ptr_to_wifi_hw_addr(desc->next) : 0);
}

static void prepare_desc_for_hardware(wifi_dma_desc_t *desc)
{
    desc->size = WIFI_RX_BUFFER_SIZE;
    desc->length = WIFI_RX_BUFFER_SIZE;
    desc->has_data = 0;
    desc->owner = 1;
}

static void recycle_desc(wifi_dma_desc_t *desc)
{
    desc->length = WIFI_RX_BUFFER_SIZE;
    desc->has_data = 0;
    desc->owner = 1;
}

void wifi_dma_rx_setup(void)
{
    ESP_LOGI(TAG, "RX DMA setup start for ESP32-C3");
    ESP_LOGI(TAG, "DMA window mask=0x%08lx", WIFI_DMA_ADDR_MASK_REG);
    ESP_LOGI(TAG, "DMA window low =0x%08lx", WIFI_DMA_HW_LOW_REG);
    ESP_LOGI(TAG, "DMA cpu base   =0x%08lx", WIFI_DMA_CPU_BASE_REG);

    if (s_rx_chain_begin != NULL) {
        for (int i = 0; i < WIFI_RX_BUFFER_COUNT; i++) {
            if (s_rx_desc[i] != NULL) {
                prepare_desc_for_hardware(s_rx_desc[i]);
            }
        }

        ESP_LOGI(TAG, "RX DMA chain already exists, descriptors rearmed");
        return;
    }

    memset(s_rx_desc, 0, sizeof(s_rx_desc));
    memset(s_dma_padding, 0, sizeof(s_dma_padding));

    s_dma_padding_count = 0;
    s_rx_chain_begin = NULL;
    s_rx_chain_last = NULL;

    wifi_dma_push_heap_into_window();

    for (int i = 0; i < WIFI_RX_BUFFER_COUNT; i++) {
        wifi_dma_desc_t *desc = wifi_dma_calloc_reachable(sizeof(wifi_dma_desc_t));
        wifi_rx_packet_t *packet = wifi_dma_calloc_reachable(WIFI_RX_BUFFER_SIZE);

        if (desc == NULL || packet == NULL) {
            ESP_LOGE(TAG, "failed to allocate RX DMA descriptor/buffer");
            abort();
        }

        if (((uint32_t)desc & 0x3) != 0) {
            ESP_LOGE(TAG, "descriptor is not 4-byte aligned: %p", desc);
            abort();
        }

        if (!wifi_dma_ptr_looks_reachable(desc, sizeof(wifi_dma_desc_t))) {
            ESP_LOGE(TAG, "descriptor not reachable by Wi-Fi DMA: %p", desc);
            abort();
        }

        if (!wifi_dma_ptr_looks_reachable(packet, WIFI_RX_BUFFER_SIZE)) {
            ESP_LOGE(TAG, "packet buffer not reachable by Wi-Fi DMA: %p", packet);
            abort();
        }

        desc->packet = packet;
        desc->next = NULL;

        prepare_desc_for_hardware(desc);

        s_rx_desc[i] = desc;

        if (s_rx_chain_begin == NULL) {
            s_rx_chain_begin = desc;
        }

        if (s_rx_chain_last != NULL) {
            s_rx_chain_last->next = desc;
        }

        s_rx_chain_last = desc;
    }

    if (s_rx_chain_last != NULL && s_rx_chain_begin != NULL) {
        s_rx_chain_last->next = s_rx_chain_begin;
    }

    ESP_LOGI(TAG, "RX DMA chain created");
    ESP_LOGI(TAG, "DMA padding blocks kept=%d", s_dma_padding_count);

    for (int i = 0; i < WIFI_RX_BUFFER_COUNT; i++) {
        log_desc(i, s_rx_desc[i]);
        dump_desc_raw(i, s_rx_desc[i]);
    }

    ESP_LOGI(TAG, "rx_chain_begin CPU=%p", s_rx_chain_begin);
    ESP_LOGI(TAG, "rx_chain_begin HW =0x%08lx", ptr_to_wifi_hw_addr(s_rx_chain_begin));
    ESP_LOGI(TAG, "rx_chain_last  CPU=%p", s_rx_chain_last);
    ESP_LOGI(TAG, "rx_chain_last  HW =0x%08lx", ptr_to_wifi_hw_addr(s_rx_chain_last));
    ESP_LOGI(TAG, "RX DMA setup done");
}

void wifi_dma_rx_attach_to_hardware(void)
{
    if (s_rx_chain_begin == NULL) {
        ESP_LOGE(TAG, "cannot attach RX DMA: chain is NULL");
        abort();
    }

    const uint32_t first_hw = ptr_to_wifi_hw_addr(s_rx_chain_begin);

    ESP_LOGI(TAG, "attaching RX DMA chain to hardware");
    ESP_LOGI(TAG, "rx_chain_begin CPU addr=%p", s_rx_chain_begin);
    ESP_LOGI(TAG, "rx_chain_begin HW  addr=0x%08lx", first_hw);

    WIFI_BASE_RX_DSCR = first_hw;
    WIFI_NEXT_RX_DSCR = first_hw;
    WIFI_RX_DSCR_A = first_hw;
    WIFI_RX_DSCR_B = first_hw;
    WIFI_LAST_RX_DSCR = 0;

    WIFI_RX_POLICY_0 = 0x00007045;
    WIFI_RX_POLICY_1 = 0x00007045;

    uint32_t base_now = WIFI_BASE_RX_DSCR;
    uint32_t next_now = WIFI_NEXT_RX_DSCR;
    uint32_t last_now = WIFI_LAST_RX_DSCR;

    ESP_LOGI(TAG, "WIFI_BASE_RX_DSCR=0x%08lx", base_now);
    ESP_LOGI(TAG, "WIFI_NEXT_RX_DSCR=0x%08lx", next_now);
    ESP_LOGI(TAG, "WIFI_LAST_RX_DSCR=0x%08lx", last_now);
    ESP_LOGI(TAG, "WIFI_RX_DSCR_A   =0x%08lx", WIFI_RX_DSCR_A);
    ESP_LOGI(TAG, "WIFI_RX_DSCR_B   =0x%08lx", WIFI_RX_DSCR_B);
    ESP_LOGI(TAG, "WIFI_RX_POLICY_0 =0x%08lx", WIFI_RX_POLICY_0);
    ESP_LOGI(TAG, "WIFI_RX_POLICY_1 =0x%08lx", WIFI_RX_POLICY_1);

    if (base_now != first_hw) {
        ESP_LOGW(TAG,
                 "RX DMA BASE changed/was not latched: expected=0x%08lx got=0x%08lx",
                 first_hw,
                 base_now);
    }

    if (next_now != first_hw) {
        ESP_LOGW(TAG, "RX DMA NEXT already moved: first=0x%08lx now=0x%08lx", first_hw, next_now);
    }

    ESP_LOGI(TAG, "RX DMA attach done");
}

void wifi_dma_rx_kick_hardware(void)
{
    ESP_LOGI(TAG, "kicking RX hardware");

    uint32_t before = WIFI_MAC_BITMASK_084;

    WIFI_MAC_BITMASK_084 = before | 0x00000001;

    int timeout = 100000;

    while ((WIFI_MAC_BITMASK_084 & 0x00000001) && timeout > 0) {
        timeout--;
    }

    ESP_LOGI(TAG, "WIFI_MAC_BITMASK_084 before=0x%08lx", before);
    ESP_LOGI(TAG, "WIFI_MAC_BITMASK_084 after =0x%08lx timeout_left=%d", WIFI_MAC_BITMASK_084, timeout);
}

void wifi_dma_dump_registers(const char *label)
{
    ESP_LOGW(TAG, "---- RX DMA dump: %s ----", label ? label : "no label");
    ESP_LOGI(TAG, "BASE     = 0x%08lx", WIFI_BASE_RX_DSCR);
    ESP_LOGI(TAG, "NEXT     = 0x%08lx", WIFI_NEXT_RX_DSCR);
    ESP_LOGI(TAG, "LAST     = 0x%08lx", WIFI_LAST_RX_DSCR);
    ESP_LOGI(TAG, "DSCR_A   = 0x%08lx", WIFI_RX_DSCR_A);
    ESP_LOGI(TAG, "DSCR_B   = 0x%08lx", WIFI_RX_DSCR_B);
    ESP_LOGI(TAG, "BIT084   = 0x%08lx", WIFI_MAC_BITMASK_084);
    ESP_LOGI(TAG, "INT_ST   = 0x%08lx", WIFI_DMA_INT_STATUS);
    ESP_LOGI(TAG, "POLICY0  = 0x%08lx", WIFI_RX_POLICY_0);
    ESP_LOGI(TAG, "POLICY1  = 0x%08lx", WIFI_RX_POLICY_1);
    ESP_LOGI(TAG, "WIN_MASK = 0x%08lx", WIFI_DMA_ADDR_MASK_REG);
    ESP_LOGI(TAG, "WIN_LOW  = 0x%08lx", WIFI_DMA_HW_LOW_REG);
    ESP_LOGI(TAG, "CPU_BASE = 0x%08lx", WIFI_DMA_CPU_BASE_REG);
    ESP_LOGW(TAG, "-----------------------------");
}

void wifi_dma_rx_handle_ready(void)
{
    bool found_any = false;

    static int poll_count;
    poll_count++;

    if ((poll_count % 50) == 0) {
        ESP_LOGW(TAG, "---- RX descriptor raw dump ----");

        for (int i = 0; i < WIFI_RX_BUFFER_COUNT; i++) {
            dump_desc_raw(i, s_rx_desc[i]);
        }
    }

    for (int i = 0; i < WIFI_RX_BUFFER_COUNT; i++) {
        wifi_dma_desc_t *desc = s_rx_desc[i];

        if (desc == NULL || !desc->has_data) {
            continue;
        }

        found_any = true;

        uint32_t rx_len = desc->length;

        ESP_LOGD(TAG,
                 "RX descriptor has data: index=%d desc=%p hw=0x%08lx len=%lu size=%u "
                 "owner=%u packet=%p packet_hw=0x%08lx",
                 i,
                 desc,
                 ptr_to_wifi_hw_addr(desc),
                 rx_len,
                 desc->size,
                 desc->owner,
                 desc->packet,
                 ptr_to_wifi_hw_addr(desc->packet));

        if (rx_len == 0 || rx_len > WIFI_RX_BUFFER_SIZE) {
            ESP_LOGE(TAG, "bad RX descriptor length: %lu", rx_len);
            recycle_desc(desc);
            continue;
        }

        uint8_t *raw_packet = (uint8_t *)desc->packet;

        if (raw_packet == NULL) {
            ESP_LOGE(TAG, "RX packet pointer is NULL");
            recycle_desc(desc);
            continue;
        }

        pf_packet_history_capture(raw_packet, rx_len, false);
        recycle_desc(desc);
    }

    if (!found_any) {
        ESP_LOGD(TAG, "RX ready handler called, but no descriptor has_data=1");
    }
}

void wifi_dma_rx_poll_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "RX polling task started");

    uint32_t last_int = 0xffffffff;
    uint32_t last_next = 0xffffffff;
    uint32_t last_last = 0xffffffff;

    while (true) {
        uint32_t now_int = WIFI_DMA_INT_STATUS;
        uint32_t now_next = WIFI_NEXT_RX_DSCR;
        uint32_t now_last = WIFI_LAST_RX_DSCR;

        if (now_int != last_int || now_next != last_next || now_last != last_last) {
            ESP_LOGD(TAG,
                     "DMA changed: INT 0x%08lx->0x%08lx NEXT 0x%08lx->0x%08lx "
                     "LAST 0x%08lx->0x%08lx",
                     last_int,
                     now_int,
                     last_next,
                     now_next,
                     last_last,
                     now_last);

            last_int = now_int;
            last_next = now_next;
            last_last = now_last;
        }

        wifi_dma_rx_handle_ready();

        if (now_int != 0) {
            WIFI_DMA_INT_CLR = now_int;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

bool wifi_dma_rx_start_polling(void)
{
    if (s_rx_poll_task != NULL) {
        ESP_LOGW(TAG, "RX polling task already running");
        return true;
    }

    BaseType_t task_ok = xTaskCreate(wifi_dma_rx_poll_task, "rx_poll", 4096, NULL, 6, &s_rx_poll_task);

    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "failed to start RX polling task");
        s_rx_poll_task = NULL;
        return false;
    }

    return true;
}

void wifi_dma_rx_stop_polling(void)
{
    if (s_rx_poll_task == NULL) {
        return;
    }

    TaskHandle_t task = s_rx_poll_task;
    s_rx_poll_task = NULL;
    vTaskDelete(task);
}
