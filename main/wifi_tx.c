#include "wifi_tx.h"

#include "pf.h"
#include "wifi_power.h"
#include "wifi_regs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_tx";

#define WIFI_TX_LOGI(...) do { if (pf_debug_enabled()) { ESP_LOGI(__VA_ARGS__); } } while (0)
#define WIFI_TX_LOGW(...) do { if (pf_debug_enabled()) { ESP_LOGW(__VA_ARGS__); } } while (0)

#define WIFI_TX_DMA_HW_ADDR_MASK 0x000fffffU
#define WIFI_TX_DMA_DRAM_WINDOW 0x00600000U
#define WIFI_TX_BUFFER_SIZE 512U
#define WIFI_TX_WAIT_TIMEOUT_MS 350U
#define WIFI_TX_STARTUP_SETTLE_MS 20U
#define WIFI_TX_DESC_OVERHEAD 32U
#define WIFI_TX_FCS_LEN 4U
#define WIFI_TX_LEGACY_RATE_1M 0U
#define WIFI_TX_CONFIG_READY_BITS WIFI_TX_ORACLE_IDLE_CFG
#define WIFI_TX_KICK_BITS 0xc0000000U
#define WIFI_TX_PLCP1_PREFIX 0x00400000U
#define WIFI_TX_PLCP2_DEFAULT 0x00400020U
#define WIFI_TX_DURATION_DEFAULT 0x00400000U
#define WIFI_TX_INT_DONE 0x00000080U
#define WIFI_TX_FIRST_USABLE_SLOT 3U
#define WIFI_TX_USABLE_SLOT_COUNT 1U

#define WIFI_TX_ORACLE_KICK_CFG 0x020003ffU
#define WIFI_TX_ORACLE_IDLE_CFG 0x020003ffU
#define WIFI_TX_ORACLE_LEN_A_PREFIX 0x20000000U
#define WIFI_TX_ORACLE_CONTROL_DEFAULT 0x00b00000U
#define WIFI_TX_ORACLE_CONTROL_2_DEFAULT 0x00000001U
#define WIFI_TX_ORACLE_PMD0_DEFAULT 0xecdadf4eU
#define WIFI_TX_ORACLE_PMD1_DEFAULT 0x04304f1cU
#define WIFI_TX_ORACLE_PMD2_DEFAULT 0xbe6ead27U
#define WIFI_TX_ORACLE_PMD3_DEFAULT 0x1061022bU
#define WIFI_TX_ORACLE_PMD4_DEFAULT 0xdc8df3deU
#define WIFI_TX_ORACLE_PMD5_DEFAULT 0xc0480a20U

typedef struct __attribute__((packed, aligned(4))) pf_tx_dma_desc {
    uint32_t control;
    uint8_t *packet;
    struct pf_tx_dma_desc *next;
} pf_tx_dma_desc_t;

_Static_assert(sizeof(pf_tx_dma_desc_t) == 12, "TX DMA descriptor layout must be 12 bytes");

typedef struct {
    pf_tx_dma_desc_t *desc;
    uint8_t *frame;
    bool allocated;
    bool in_use;
    uint32_t attempts;
} wifi_tx_slot_t;

static wifi_tx_slot_t s_slots[WIFI_TX_SLOT_COUNT];
static bool s_tx_ready;
static uint8_t s_current_channel;
static uint8_t s_next_slot;
static uint32_t s_wait_q1_baseline;
static uint32_t s_wait_q2_baseline;
static uint32_t s_tx_trace_sequence;
static uint32_t s_phy_control_template = WIFI_TX_ORACLE_CONTROL_DEFAULT;
static uint32_t s_phy_control_2_template = WIFI_TX_ORACLE_CONTROL_2_DEFAULT;
static uint32_t s_phy_pmd_template[6] = {
    WIFI_TX_ORACLE_PMD0_DEFAULT,
    WIFI_TX_ORACLE_PMD1_DEFAULT,
    WIFI_TX_ORACLE_PMD2_DEFAULT,
    WIFI_TX_ORACLE_PMD3_DEFAULT,
    WIFI_TX_ORACLE_PMD4_DEFAULT,
    WIFI_TX_ORACLE_PMD5_DEFAULT,
};

extern int chip_v7_set_chan(uint8_t primary, uint8_t secondary);


static uint32_t wifi_tx_desc_control(uint32_t size, uint32_t length)
{
    return (size & 0x0fffU) | ((length & 0x0fffU) << 12) | (1UL << 30) | (1UL << 31);
}

static uint32_t wifi_tx_desc_size(const pf_tx_dma_desc_t *desc)
{
    return desc->control & 0x0fffU;
}

static uint32_t wifi_tx_desc_length(const pf_tx_dma_desc_t *desc)
{
    return (desc->control >> 12) & 0x0fffU;
}

static uint32_t wifi_tx_desc_has_data(const pf_tx_dma_desc_t *desc)
{
    return (desc->control >> 30) & 0x01U;
}

static uint32_t wifi_tx_desc_owner(const pf_tx_dma_desc_t *desc)
{
    return (desc->control >> 31) & 0x01U;
}

static uint32_t wifi_tx_crc32_le(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xffffffffU;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 1U) != 0) {
                crc = (crc >> 1) ^ 0xedb88320U;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static void wifi_tx_write_fcs(uint8_t *frame, uint32_t len)
{
    uint32_t fcs = wifi_tx_crc32_le(frame, len);

    frame[len] = (uint8_t)(fcs & 0xffU);
    frame[len + 1] = (uint8_t)((fcs >> 8) & 0xffU);
    frame[len + 2] = (uint8_t)((fcs >> 16) & 0xffU);
    frame[len + 3] = (uint8_t)((fcs >> 24) & 0xffU);
}

static uint32_t ptr_to_wifi_hw_addr(const void *ptr)
{
    return WIFI_TX_DMA_DRAM_WINDOW | (((uint32_t)ptr) & WIFI_TX_DMA_HW_ADDR_MASK);
}

static volatile uint32_t *wifi_tx_slot_reg(volatile uint32_t *base, int offset_words, uint8_t slot)
{
    return base + (offset_words * slot);
}

static volatile uint32_t *wifi_tx_config_reg(uint8_t slot)
{
    return wifi_tx_slot_reg(WIFI_TX_CONFIG_BASE, WIFI_TX_CONFIG_OFFSET_WORDS, slot);
}

static volatile uint32_t *wifi_tx_plcp0_reg(uint8_t slot)
{
    return wifi_tx_slot_reg(WIFI_MAC_TX_PLCP0_BASE, WIFI_MAC_TX_PLCP0_OFFSET_WORDS, slot);
}

static volatile uint32_t *wifi_tx_plcp1_reg(uint8_t slot)
{
    return wifi_tx_slot_reg(WIFI_MAC_TX_PLCP1_BASE, WIFI_MAC_TX_PLCP1_OFFSET_WORDS, slot);
}

static volatile uint32_t *wifi_tx_plcp2_reg(uint8_t slot)
{
    return wifi_tx_slot_reg(WIFI_MAC_TX_PLCP2_BASE, WIFI_MAC_TX_PLCP2_OFFSET_WORDS, slot);
}

static volatile uint32_t *wifi_tx_duration_reg(uint8_t slot)
{
    return wifi_tx_slot_reg(WIFI_MAC_TX_DURATION_BASE, WIFI_MAC_TX_DURATION_OFFSET_WORDS, slot);
}

static void wifi_tx_mmio_fence(void)
{
    __asm__ __volatile__("fence" ::: "memory");
}



static uint32_t wifi_tx_slot_q1_done_bit(uint8_t slot)
{
    if (slot == 3) {
        return 0x00000001U;
    }

    if (slot == 4) {
        return 0x00000002U;
    }

    return 0;
}
static uint32_t wifi_tx_slot_q2_done_bit(uint8_t slot)
{
    if (slot == 3) {
        return 0x00000001U;
    }

    if (slot == 4) {
        return 0x00000002U;
    }

    return 0;
}
static uint32_t wifi_tx_slot_complete_bit(uint8_t slot)
{
    return 1UL << slot;
}

static void wifi_tx_clear_complete(uint32_t mask)
{
    if (mask != 0) {
        WIFI_TXQ_CLR_STATE_COMPLETE = mask;
    }
}

static void wifi_tx_clear_error(uint32_t mask)
{
    if (mask != 0) {
        WIFI_TXQ_CLR_STATE_ERROR = mask;
    }
}

static uint32_t wifi_txq_get_state(uint8_t queue_id)
{
    if (queue_id == 0) {
        return WIFI_TXQ_GET_STATE_0 & 0x000007ffU;
    }

    if (queue_id == 1) {
        return (WIFI_TXQ_GET_STATE_0 >> 16) & 0x000000ffU;
    }

    if (queue_id == 2) {
        return WIFI_TXQ_GET_STATE_2 & 0x0000000fU;
    }

    return 0;
}

static void wifi_txq_clear_state(uint8_t queue_id, uint32_t state)
{
    if (state == 0) {
        return;
    }

    if (queue_id == 0) {
        WIFI_TXQ_CLR_STATE_0 = (WIFI_TXQ_CLR_STATE_0 & 0xfffff800U) | (state & 0x000007ffU);
        return;
    }

    if (queue_id == 1) {
        WIFI_TXQ_CLR_STATE_0 = (WIFI_TXQ_CLR_STATE_0 & 0xff00ffffU) | ((state & 0x000000ffU) << 16);
        return;
    }

    if (queue_id == 2) {
        WIFI_TXQ_CLR_STATE_2 |= state;
    }
}

static uint32_t wifi_txq_enable_word(uint8_t queue_id)
{
    return *((volatile uint32_t *)(0x60033d08U - ((uint32_t)queue_id * 8U)));
}

static void wifi_tx_log_frame_head(const char *label, const uint8_t *frame, uint32_t len)
{
    WIFI_TX_LOGI(TAG,
             "%s frame len=%lu head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             label,
             (unsigned long)len,
             len > 0 ? frame[0] : 0,
             len > 1 ? frame[1] : 0,
             len > 2 ? frame[2] : 0,
             len > 3 ? frame[3] : 0,
             len > 4 ? frame[4] : 0,
             len > 5 ? frame[5] : 0,
             len > 6 ? frame[6] : 0,
             len > 7 ? frame[7] : 0,
             len > 8 ? frame[8] : 0,
             len > 9 ? frame[9] : 0,
             len > 10 ? frame[10] : 0,
             len > 11 ? frame[11] : 0,
             len > 12 ? frame[12] : 0,
             len > 13 ? frame[13] : 0,
             len > 14 ? frame[14] : 0,
             len > 15 ? frame[15] : 0);
}

static void wifi_tx_log_desc(uint8_t slot, const char *label)
{
    wifi_tx_slot_t *tx_slot = &s_slots[slot];
    const uint32_t *raw = (const uint32_t *)tx_slot->desc;

    WIFI_TX_LOGI(TAG,
             "%s slot=%u desc=%p desc_hw=0x%08lx frame=%p frame_hw=0x%08lx raw=%08lx %08lx %08lx size=%u length=%u owner=%u has_data=%u packet=%p next=%p attempts=%lu",
             label,
             (unsigned)slot,
             tx_slot->desc,
             (unsigned long)ptr_to_wifi_hw_addr(tx_slot->desc),
             tx_slot->frame,
             (unsigned long)ptr_to_wifi_hw_addr(tx_slot->frame),
             (unsigned long)raw[0],
             (unsigned long)raw[1],
             (unsigned long)raw[2],
             (unsigned)wifi_tx_desc_size(tx_slot->desc),
             (unsigned)wifi_tx_desc_length(tx_slot->desc),
             (unsigned)wifi_tx_desc_owner(tx_slot->desc),
             (unsigned)wifi_tx_desc_has_data(tx_slot->desc),
             tx_slot->desc->packet,
             tx_slot->desc->next,
             (unsigned long)tx_slot->attempts);
}

static void wifi_tx_log_slot(uint8_t slot, const char *label)
{
    WIFI_TX_LOGI(TAG,
             "%s slot=%u cfg=0x%08lx plcp0=0x%08lx plcp1=0x%08lx plcp2=0x%08lx dur=0x%08lx complete=0x%08lx error=0x%08lx int=0x%08lx q0=0x%08lx q1=0x%08lx q2=0x%08lx q_en=%08lx/%08lx/%08lx/%08lx/%08lx bit084=0x%08lx mac_ctrl=0x%08lx",
             label,
             (unsigned)slot,
             (unsigned long)*wifi_tx_config_reg(slot),
             (unsigned long)*wifi_tx_plcp0_reg(slot),
             (unsigned long)*wifi_tx_plcp1_reg(slot),
             (unsigned long)*wifi_tx_plcp2_reg(slot),
             (unsigned long)*wifi_tx_duration_reg(slot),
             (unsigned long)WIFI_TXQ_GET_STATE_COMPLETE,
             (unsigned long)WIFI_TXQ_GET_STATE_ERROR,
             (unsigned long)WIFI_DMA_INT_STATUS,
             (unsigned long)wifi_txq_get_state(0),
             (unsigned long)wifi_txq_get_state(1),
             (unsigned long)wifi_txq_get_state(2),
             (unsigned long)wifi_txq_enable_word(0),
             (unsigned long)wifi_txq_enable_word(1),
             (unsigned long)wifi_txq_enable_word(2),
             (unsigned long)wifi_txq_enable_word(3),
             (unsigned long)wifi_txq_enable_word(4),
             (unsigned long)WIFI_MAC_BITMASK_084,
             (unsigned long)WIFI_MAC_CTRL_REG);
}


static void wifi_tx_log_observed(const char *label)
{
    WIFI_TX_LOGI(TAG,
             "%s observed cfg=0x%08lx buf=0x%08lx len_a=0x%08lx dur=0x%08lx plcp1=0x%08lx plcp2=0x%08lx ctrl=0x%08lx ctrl2=0x%08lx pmd=%08lx %08lx %08lx %08lx %08lx %08lx",
             label,
             (unsigned long)WIFI_TX_OBSERVED_CONFIG,
             (unsigned long)WIFI_TX_OBSERVED_BUFFER,
             (unsigned long)WIFI_TX_OBSERVED_LEN_A,
             (unsigned long)WIFI_TX_OBSERVED_DURATION,
             (unsigned long)WIFI_TX_OBSERVED_PLCP1,
             (unsigned long)WIFI_TX_OBSERVED_PLCP2,
             (unsigned long)WIFI_TX_OBSERVED_CONTROL,
             (unsigned long)WIFI_TX_OBSERVED_CONTROL_2,
             (unsigned long)WIFI_TX_OBSERVED_PMD_0,
             (unsigned long)WIFI_TX_OBSERVED_PMD_1,
             (unsigned long)WIFI_TX_OBSERVED_PMD_2,
             (unsigned long)WIFI_TX_OBSERVED_PMD_3,
             (unsigned long)WIFI_TX_OBSERVED_PMD_4,
             (unsigned long)WIFI_TX_OBSERVED_PMD_5);
}
static uint32_t wifi_tx_observed_pmd_or(void)
{
    return WIFI_TX_OBSERVED_PMD_0 |
           WIFI_TX_OBSERVED_PMD_1 |
           WIFI_TX_OBSERVED_PMD_2 |
           WIFI_TX_OBSERVED_PMD_3 |
           WIFI_TX_OBSERVED_PMD_4 |
           WIFI_TX_OBSERVED_PMD_5;
}

static void wifi_tx_capture_phy_template(const char *label)
{
    uint32_t pmd_or = wifi_tx_observed_pmd_or();

    if (WIFI_TX_OBSERVED_CONTROL == 0 || pmd_or == 0) {
        WIFI_TX_LOGW(TAG,
                     "%s phy-template ignored ctrl=0x%08lx ctrl2=0x%08lx pmd_or=0x%08lx",
                     label,
                     (unsigned long)WIFI_TX_OBSERVED_CONTROL,
                     (unsigned long)WIFI_TX_OBSERVED_CONTROL_2,
                     (unsigned long)pmd_or);
        return;
    }

    s_phy_control_template = WIFI_TX_OBSERVED_CONTROL;
    s_phy_control_2_template = WIFI_TX_OBSERVED_CONTROL_2;
    s_phy_pmd_template[0] = WIFI_TX_OBSERVED_PMD_0;
    s_phy_pmd_template[1] = WIFI_TX_OBSERVED_PMD_1;
    s_phy_pmd_template[2] = WIFI_TX_OBSERVED_PMD_2;
    s_phy_pmd_template[3] = WIFI_TX_OBSERVED_PMD_3;
    s_phy_pmd_template[4] = WIFI_TX_OBSERVED_PMD_4;
    s_phy_pmd_template[5] = WIFI_TX_OBSERVED_PMD_5;

    WIFI_TX_LOGI(TAG,
                 "%s phy-template ctrl=0x%08lx ctrl2=0x%08lx pmd=%08lx %08lx %08lx %08lx %08lx %08lx",
                 label,
                 (unsigned long)s_phy_control_template,
                 (unsigned long)s_phy_control_2_template,
                 (unsigned long)s_phy_pmd_template[0],
                 (unsigned long)s_phy_pmd_template[1],
                 (unsigned long)s_phy_pmd_template[2],
                 (unsigned long)s_phy_pmd_template[3],
                 (unsigned long)s_phy_pmd_template[4],
                 (unsigned long)s_phy_pmd_template[5]);
}

static void wifi_tx_log_oracle_diff(uint8_t slot, const char *label, uint32_t len, bool kicked)
{
    uint32_t air_len = len + 4U;
    uint32_t expected_cfg = kicked ? WIFI_TX_ORACLE_KICK_CFG : WIFI_TX_ORACLE_IDLE_CFG;
    uint32_t expected_len_a = 0x20000000U | (air_len & 0x0fffU);
    uint32_t expected_plcp1 = WIFI_TX_PLCP1_PREFIX | (air_len & 0x0fffU) | ((WIFI_TX_LEGACY_RATE_1M & 0x1fU) << 12);
    uint32_t observed_pmd_or = WIFI_TX_OBSERVED_PMD_0 |
                               WIFI_TX_OBSERVED_PMD_1 |
                               WIFI_TX_OBSERVED_PMD_2 |
                               WIFI_TX_OBSERVED_PMD_3 |
                               WIFI_TX_OBSERVED_PMD_4 |
                               WIFI_TX_OBSERVED_PMD_5;

    WIFI_TX_LOGI(TAG,
             "%s oracle-diff trace=%lu slot=%u expected cfg=0x%08lx len_a=0x%08lx dur=0x%08lx plcp1=0x%08lx plcp2=0x%08lx",
             label,
             (unsigned long)s_tx_trace_sequence,
             (unsigned)slot,
             (unsigned long)expected_cfg,
             (unsigned long)expected_len_a,
             (unsigned long)WIFI_TX_DURATION_DEFAULT,
             (unsigned long)expected_plcp1,
             (unsigned long)WIFI_TX_PLCP2_DEFAULT);
    WIFI_TX_LOGI(TAG,
             "%s oracle-diff trace=%lu observed cfg=0x%08lx buf=0x%08lx len_a=0x%08lx dur=0x%08lx plcp1=0x%08lx plcp2=0x%08lx ctrl=0x%08lx ctrl2=0x%08lx pmd_or=0x%08lx complete=0x%08lx error=0x%08lx int=0x%08lx q=%08lx/%08lx/%08lx",
             label,
             (unsigned long)s_tx_trace_sequence,
             (unsigned long)WIFI_TX_OBSERVED_CONFIG,
             (unsigned long)WIFI_TX_OBSERVED_BUFFER,
             (unsigned long)WIFI_TX_OBSERVED_LEN_A,
             (unsigned long)WIFI_TX_OBSERVED_DURATION,
             (unsigned long)WIFI_TX_OBSERVED_PLCP1,
             (unsigned long)WIFI_TX_OBSERVED_PLCP2,
             (unsigned long)WIFI_TX_OBSERVED_CONTROL,
             (unsigned long)WIFI_TX_OBSERVED_CONTROL_2,
             (unsigned long)observed_pmd_or,
             (unsigned long)WIFI_TXQ_GET_STATE_COMPLETE,
             (unsigned long)WIFI_TXQ_GET_STATE_ERROR,
             (unsigned long)WIFI_DMA_INT_STATUS,
             (unsigned long)wifi_txq_get_state(0),
             (unsigned long)wifi_txq_get_state(1),
             (unsigned long)wifi_txq_get_state(2));

    if ((WIFI_TX_OBSERVED_CONFIG & 0x02000000U) == 0) {
        WIFI_TX_LOGW(TAG, "%s oracle-diff: observed TX window is not enabled", label);
    }

    if ((WIFI_TX_OBSERVED_LEN_A & 0x0fffU) != (expected_len_a & 0x0fffU)) {
        WIFI_TX_LOGW(TAG,
                 "%s oracle-diff: observed length mismatch expected_air_len=%lu observed_len_a=0x%08lx",
                 label,
                 (unsigned long)air_len,
                 (unsigned long)WIFI_TX_OBSERVED_LEN_A);
    }

    if (WIFI_TX_OBSERVED_CONTROL == 0 || observed_pmd_or == 0) {
        WIFI_TX_LOGW(TAG,
                 "%s oracle-diff: control/PMD are empty; this usually means DMA accepted the frame but PHY is not emitting it",
                 label);
    }
}
static void wifi_tx_log_all_slots(const char *label)
{
    for (uint8_t slot = 0; slot < WIFI_TX_SLOT_COUNT; slot++) {
        WIFI_TX_LOGI(TAG,
                 "%s slot=%u cfg=0x%08lx plcp0=0x%08lx plcp1=0x%08lx plcp2=0x%08lx dur=0x%08lx",
                 label,
                 (unsigned)slot,
                 (unsigned long)*wifi_tx_config_reg(slot),
                 (unsigned long)*wifi_tx_plcp0_reg(slot),
                 (unsigned long)*wifi_tx_plcp1_reg(slot),
                 (unsigned long)*wifi_tx_plcp2_reg(slot),
                 (unsigned long)*wifi_tx_duration_reg(slot));
    }
}

static void wifi_tx_log_oracle_reference(void)
{
    WIFI_TX_LOGI(TAG,
             "TX model: OpenMAC-style DMA descriptor path. Oracle official TX usually shows kick_cfg=0x%08lx idle_cfg=0x%08lx, plcp0 has kick bits while active, plcp1 carries len/rate.",
             (unsigned long)WIFI_TX_ORACLE_KICK_CFG,
             (unsigned long)WIFI_TX_ORACLE_IDLE_CFG);
}

static bool wifi_tx_apply_channel(void)
{
    pf_channel_config_t channel = pf_get_channel_config();
    uint8_t primary = 1;

    if (channel.configured && channel.primary >= 1 && channel.primary <= 13) {
        primary = channel.primary;
    }

    if (s_current_channel == primary) {
        return true;
    }

    WIFI_TX_LOGI(TAG, "TX set channel primary=%u secondary=0", (unsigned)primary);
    if (chip_v7_set_chan(primary, 0) != 0) {
        ESP_LOGE(TAG, "TX set channel failed primary=%u", (unsigned)primary);
        return false;
    }

    s_current_channel = primary;
    return true;
}

static bool wifi_tx_alloc_slot(uint8_t slot)
{
    wifi_tx_slot_t *tx_slot = &s_slots[slot];

    tx_slot->desc = heap_caps_calloc(1, sizeof(pf_tx_dma_desc_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    tx_slot->frame = heap_caps_calloc(1, WIFI_TX_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (tx_slot->desc == NULL || tx_slot->frame == NULL) {
        ESP_LOGE(TAG, "TX slot=%u allocation failed desc=%p frame=%p", (unsigned)slot, tx_slot->desc, tx_slot->frame);
        return false;
    }

    tx_slot->allocated = true;
    tx_slot->in_use = false;

    WIFI_TX_LOGI(TAG,
             "TX slot=%u allocated desc=%p desc_hw=0x%08lx frame=%p frame_hw=0x%08lx desc_size=%u desc_align=%lu frame_align=%lu",
             (unsigned)slot,
             tx_slot->desc,
             (unsigned long)ptr_to_wifi_hw_addr(tx_slot->desc),
             tx_slot->frame,
             (unsigned long)ptr_to_wifi_hw_addr(tx_slot->frame),
             (unsigned)sizeof(pf_tx_dma_desc_t),
             (unsigned long)((uint32_t)tx_slot->desc & 0x03U),
             (unsigned long)((uint32_t)tx_slot->frame & 0x03U));
    return true;
}

static bool wifi_tx_setup(void)
{
    if (s_tx_ready) {
        return true;
    }

    WIFI_TX_LOGI(TAG, "TX setup begin: native MMIO path, no esp_wifi_80211_tx");
    wifi_power_enable_minimal();
    vTaskDelay(pdMS_TO_TICKS(WIFI_TX_STARTUP_SETTLE_MS));

    wifi_tx_log_oracle_reference();
    WIFI_TX_LOGI(TAG,
             "TX setup regs before mac_ctrl=0x%08lx bit084=0x%08lx int=0x%08lx complete=0x%08lx error=0x%08lx",
             (unsigned long)WIFI_MAC_CTRL_REG,
             (unsigned long)WIFI_MAC_BITMASK_084,
             (unsigned long)WIFI_DMA_INT_STATUS,
             (unsigned long)WIFI_TXQ_GET_STATE_COMPLETE,
             (unsigned long)WIFI_TXQ_GET_STATE_ERROR);

    WIFI_MAC_CTRL_REG &= 0xffffe800U;
    WIFI_MAC_BITMASK_084 |= 0x80000000U;
    wifi_tx_mmio_fence();

    for (uint8_t slot = 0; slot < WIFI_TX_SLOT_COUNT; slot++) {
        if (!wifi_tx_alloc_slot(slot)) {
            return false;
        }
    }

    s_tx_ready = true;
    s_next_slot = 0;

    wifi_tx_log_all_slots("TX setup slots");
    WIFI_TX_LOGI(TAG,
             "TX setup done mac_ctrl=0x%08lx bit084=0x%08lx",
             (unsigned long)WIFI_MAC_CTRL_REG,
             (unsigned long)WIFI_MAC_BITMASK_084);
    return true;
}

static void wifi_tx_clear_status(void)
{
    uint32_t complete = WIFI_TXQ_GET_STATE_COMPLETE;
    uint32_t error = WIFI_TXQ_GET_STATE_ERROR;
    uint32_t interrupt_status = WIFI_DMA_INT_STATUS;

    if (complete != 0) {
        WIFI_TX_LOGI(TAG, "TX clear stale complete=0x%08lx", (unsigned long)complete);
        WIFI_TXQ_CLR_STATE_COMPLETE = complete;
    }

    if (error != 0) {
        WIFI_TX_LOGI(TAG, "TX clear stale error=0x%08lx", (unsigned long)error);
        WIFI_TXQ_CLR_STATE_ERROR = error;
    }

    for (uint8_t queue = 0; queue < 3; queue++) {
        uint32_t state = wifi_txq_get_state(queue);
        if (state != 0) {
            WIFI_TX_LOGI(TAG, "TX clear stale q%u state=0x%08lx", (unsigned)queue, (unsigned long)state);
            wifi_txq_clear_state(queue, state);
        }
    }

    if (interrupt_status != 0) {
        WIFI_TX_LOGI(TAG, "TX clear stale int=0x%08lx", (unsigned long)interrupt_status);
        WIFI_DMA_INT_CLR = interrupt_status;
    }
}

static int wifi_tx_find_slot(void)
{
    for (uint8_t i = 0; i < WIFI_TX_USABLE_SLOT_COUNT; i++) {
        uint8_t index = (uint8_t)((s_next_slot + i) % WIFI_TX_USABLE_SLOT_COUNT);
        uint8_t slot = (uint8_t)(WIFI_TX_FIRST_USABLE_SLOT + index);
        if (s_slots[slot].allocated && !s_slots[slot].in_use) {
            s_next_slot = (uint8_t)((index + 1) % WIFI_TX_USABLE_SLOT_COUNT);
            return slot;
        }
    }

    return -1;
}

static bool wifi_tx_prepare_slot(uint8_t slot, const uint8_t *frame, uint32_t len)
{
    wifi_tx_slot_t *tx_slot = &s_slots[slot];
    uint32_t tx_len = len + WIFI_TX_FCS_LEN;
    uint32_t desc_size = tx_len + WIFI_TX_DESC_OVERHEAD;

    if (desc_size > 0x0fffU) {
        ESP_LOGE(TAG, "TX frame too large for descriptor len=%lu desc_size=%lu", (unsigned long)len, (unsigned long)desc_size);
        return false;
    }

    memset(tx_slot->frame, 0, WIFI_TX_BUFFER_SIZE);
    memcpy(tx_slot->frame, frame, len);
    wifi_tx_write_fcs(tx_slot->frame, len);
    memset(tx_slot->desc, 0, sizeof(*tx_slot->desc));

    tx_slot->desc->control = wifi_tx_desc_control(desc_size, tx_len);
    tx_slot->desc->packet = tx_slot->frame;
    tx_slot->desc->next = NULL;
    tx_slot->in_use = true;
    tx_slot->attempts++;

    WIFI_TX_LOGI(TAG, "TX prepare payload_len=%lu tx_len=%lu fcs=%02x %02x %02x %02x",
             (unsigned long)len,
             (unsigned long)tx_len,
             tx_slot->frame[len],
             tx_slot->frame[len + 1],
             tx_slot->frame[len + 2],
             tx_slot->frame[len + 3]);
    wifi_tx_log_frame_head("TX prepare", tx_slot->frame, tx_len);
    wifi_tx_log_desc(slot, "TX prepare");
    return true;
}

static void wifi_tx_program_slot(uint8_t slot, uint32_t len)
{
    volatile uint32_t *config = wifi_tx_config_reg(slot);
    volatile uint32_t *plcp0 = wifi_tx_plcp0_reg(slot);
    volatile uint32_t *plcp1 = wifi_tx_plcp1_reg(slot);
    volatile uint32_t *plcp2 = wifi_tx_plcp2_reg(slot);
    volatile uint32_t *duration = wifi_tx_duration_reg(slot);
    uint32_t desc_hw = ptr_to_wifi_hw_addr(s_slots[slot].desc);
    uint32_t air_len = len + 4U;
    uint32_t plcp1_value = WIFI_TX_PLCP1_PREFIX | (air_len & 0x0fffU) | ((WIFI_TX_LEGACY_RATE_1M & 0x1fU) << 12);

    wifi_tx_clear_status();
    wifi_tx_log_slot(slot, "TX before-program");
    wifi_tx_log_observed("TX before-program");

    *config = 0;
    wifi_tx_mmio_fence();
    *plcp0 = desc_hw;
    *plcp1 = plcp1_value;
    *plcp2 = WIFI_TX_PLCP2_DEFAULT;
    *duration = WIFI_TX_DURATION_DEFAULT;

    WIFI_TX_OBSERVED_LEN_A = WIFI_TX_ORACLE_LEN_A_PREFIX | (air_len & 0x0fffU);
    WIFI_TX_OBSERVED_DURATION = WIFI_TX_DURATION_DEFAULT;
    WIFI_TX_OBSERVED_PLCP1 = plcp1_value;
    WIFI_TX_OBSERVED_PLCP2 = WIFI_TX_PLCP2_DEFAULT;
    WIFI_TX_OBSERVED_CONTROL = s_phy_control_template;
    WIFI_TX_OBSERVED_CONTROL_2 = s_phy_control_2_template;
    WIFI_TX_OBSERVED_PMD_0 = s_phy_pmd_template[0];
    WIFI_TX_OBSERVED_PMD_1 = s_phy_pmd_template[1];
    WIFI_TX_OBSERVED_PMD_2 = s_phy_pmd_template[2];
    WIFI_TX_OBSERVED_PMD_3 = s_phy_pmd_template[3];
    WIFI_TX_OBSERVED_PMD_4 = s_phy_pmd_template[4];
    WIFI_TX_OBSERVED_PMD_5 = s_phy_pmd_template[5];
    wifi_tx_mmio_fence();

    *config = WIFI_TX_CONFIG_READY_BITS;
    wifi_tx_mmio_fence();

    s_wait_q1_baseline = wifi_txq_get_state(1);
    s_wait_q2_baseline = wifi_txq_get_state(2);
    wifi_tx_log_slot(slot, "TX before-kick");
    wifi_tx_log_observed("TX before-kick");
    wifi_tx_log_oracle_diff(slot, "TX before-kick", len, false);
    *config = WIFI_TX_CONFIG_READY_BITS;
    *plcp0 |= WIFI_TX_KICK_BITS;
    wifi_tx_mmio_fence();
    wifi_tx_log_slot(slot, "TX after-kick");
    wifi_tx_log_observed("TX after-kick");
    wifi_tx_log_oracle_diff(slot, "TX after-kick", len, true);
}

static bool wifi_tx_wait_done(uint8_t slot)
{
    TickType_t start = xTaskGetTickCount();
    uint32_t bit = wifi_tx_slot_complete_bit(slot);
    uint32_t q1_done_bit = wifi_tx_slot_q1_done_bit(slot);
    uint32_t q2_done_bit = wifi_tx_slot_q2_done_bit(slot);
    bool saw_kick_consumed = false;
    bool saw_interrupt = false;
    bool saw_q1_transition = false;
    bool saw_q2_transition = false;

    while ((xTaskGetTickCount() - start) <= pdMS_TO_TICKS(WIFI_TX_WAIT_TIMEOUT_MS)) {
        uint32_t complete = WIFI_TXQ_GET_STATE_COMPLETE;
        uint32_t error = WIFI_TXQ_GET_STATE_ERROR;
        uint32_t interrupt_status = WIFI_DMA_INT_STATUS;
        uint32_t plcp0 = *wifi_tx_plcp0_reg(slot);
        uint32_t queue_state_0 = wifi_txq_get_state(0);
        uint32_t queue_state_1 = wifi_txq_get_state(1);
        uint32_t queue_state_2 = wifi_txq_get_state(2);

        if ((error & bit) != 0 || error != 0) {
            ESP_LOGE(TAG,
                     "TX error slot=%u bit=0x%08lx complete=0x%08lx error=0x%08lx int=0x%08lx q0=0x%08lx q1=0x%08lx q2=0x%08lx",
                     (unsigned)slot,
                     (unsigned long)bit,
                     (unsigned long)complete,
                     (unsigned long)error,
                     (unsigned long)interrupt_status,
                     (unsigned long)queue_state_0,
                     (unsigned long)queue_state_1,
                     (unsigned long)queue_state_2);
            wifi_tx_clear_error(error);
            wifi_tx_log_slot(slot, "TX error");
            wifi_tx_log_desc(slot, "TX error");
            return false;
        }

        if ((complete & bit) != 0) {
            WIFI_TX_LOGI(TAG, "TX complete slot=%u complete=0x%08lx int=0x%08lx", (unsigned)slot, (unsigned long)complete, (unsigned long)interrupt_status);
            wifi_tx_clear_complete(complete);
            if (interrupt_status != 0) {
                WIFI_DMA_INT_CLR = interrupt_status;
            }
            wifi_tx_log_slot(slot, "TX complete");
            wifi_tx_log_oracle_diff(slot, "TX complete", wifi_tx_desc_length(s_slots[slot].desc) - WIFI_TX_FCS_LEN, false);
            wifi_tx_log_desc(slot, "TX complete");
            wifi_tx_capture_phy_template("TX complete");
            return true;
        }

        if (!saw_kick_consumed && (plcp0 & WIFI_TX_KICK_BITS) == 0) {
            saw_kick_consumed = true;
            WIFI_TX_LOGI(TAG,
                     "TX kick consumed slot=%u plcp0=0x%08lx; waiting for complete bit=0x%08lx",
                     (unsigned)slot,
                     (unsigned long)plcp0,
                     (unsigned long)bit);
            *wifi_tx_config_reg(slot) = WIFI_TX_CONFIG_READY_BITS;
            wifi_tx_mmio_fence();
            wifi_tx_log_slot(slot, "TX kick-consumed");
        }

        if (interrupt_status != 0 && !saw_interrupt) {
            saw_interrupt = true;
            WIFI_TX_LOGI(TAG,
                     "TX interrupt slot=%u int=0x%08lx done_bit=%u complete=0x%08lx error=0x%08lx q0=0x%08lx q1=0x%08lx q2=0x%08lx",
                     (unsigned)slot,
                     (unsigned long)interrupt_status,
                     (unsigned)((interrupt_status & WIFI_TX_INT_DONE) != 0),
                     (unsigned long)complete,
                     (unsigned long)error,
                     (unsigned long)queue_state_0,
                     (unsigned long)queue_state_1,
                     (unsigned long)queue_state_2);
        }

        if (interrupt_status != 0) {
            WIFI_DMA_INT_CLR = interrupt_status;
        }

        if (q1_done_bit != 0 && ((queue_state_1 & q1_done_bit) != (s_wait_q1_baseline & q1_done_bit))) {
            if (!saw_q1_transition) {
                saw_q1_transition = true;
                WIFI_TX_LOGW(TAG,
                         "TX q1-transition slot=%u q1=0x%08lx baseline=0x%08lx bit=0x%08lx complete=0x%08lx error=0x%08lx int=0x%08lx; not treating this as RF TX success",
                         (unsigned)slot,
                         (unsigned long)queue_state_1,
                         (unsigned long)s_wait_q1_baseline,
                         (unsigned long)q1_done_bit,
                         (unsigned long)complete,
                         (unsigned long)error,
                         (unsigned long)interrupt_status);
                wifi_tx_log_slot(slot, "TX q1-transition");
                wifi_tx_log_observed("TX q1-transition");
                wifi_tx_log_oracle_diff(slot, "TX q1-transition", wifi_tx_desc_length(s_slots[slot].desc) - WIFI_TX_FCS_LEN, true);
                wifi_tx_log_desc(slot, "TX q1-transition");
            }
            wifi_txq_clear_state(1, queue_state_1);
            if (interrupt_status != 0) {
                WIFI_DMA_INT_CLR = interrupt_status;
            }
        }
        if (q2_done_bit != 0 && ((queue_state_2 & q2_done_bit) != (s_wait_q2_baseline & q2_done_bit))) {
            saw_q2_transition = true;
            WIFI_TX_LOGI(TAG,
                     "TX native-complete slot=%u q2=0x%08lx baseline=0x%08lx bit=0x%08lx complete=0x%08lx error=0x%08lx int=0x%08lx",
                     (unsigned)slot,
                     (unsigned long)queue_state_2,
                     (unsigned long)s_wait_q2_baseline,
                     (unsigned long)q2_done_bit,
                     (unsigned long)complete,
                     (unsigned long)error,
                     (unsigned long)interrupt_status);
            wifi_txq_clear_state(2, queue_state_2);
            if (interrupt_status != 0) {
                WIFI_DMA_INT_CLR = interrupt_status;
            }
            wifi_tx_log_slot(slot, "TX native-complete");
            wifi_tx_log_observed("TX native-complete");
            wifi_tx_log_oracle_diff(slot, "TX native-complete", wifi_tx_desc_length(s_slots[slot].desc) - WIFI_TX_FCS_LEN, true);
            wifi_tx_log_desc(slot, "TX native-complete");
            wifi_tx_capture_phy_template("TX native-complete");
            return true;
        }

        if (queue_state_0 != 0 || queue_state_1 != 0 || queue_state_2 != 0) {
            WIFI_TX_LOGI(TAG,
                     "TX queue-state poll slot=%u q0=0x%08lx q1=0x%08lx q2=0x%08lx complete=0x%08lx error=0x%08lx int=0x%08lx",
                     (unsigned)slot,
                     (unsigned long)queue_state_0,
                     (unsigned long)queue_state_1,
                     (unsigned long)queue_state_2,
                     (unsigned long)complete,
                     (unsigned long)error,
                     (unsigned long)interrupt_status);
            wifi_txq_clear_state(0, queue_state_0);
            wifi_txq_clear_state(1, queue_state_1);
            wifi_txq_clear_state(2, queue_state_2);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGE(TAG, "TX timeout slot=%u: no complete bit after %u ms", (unsigned)slot, (unsigned)WIFI_TX_WAIT_TIMEOUT_MS);
    if (saw_q1_transition || saw_q2_transition) {
        ESP_LOGE(TAG, "TX diagnosis slot=%u: DMA/queue transition happened, but no RF completion/interrupt was observed", (unsigned)slot);
    }
    wifi_tx_log_slot(slot, "TX timeout");
    wifi_tx_log_observed("TX timeout");
    wifi_tx_log_oracle_diff(slot, "TX timeout", wifi_tx_desc_length(s_slots[slot].desc) - WIFI_TX_FCS_LEN, true);
    wifi_tx_log_desc(slot, "TX timeout");
    wifi_tx_log_all_slots("TX timeout all-slots");
    return false;
}

bool wifi_tx_raw(const uint8_t *frame, uint32_t len)
{
    int slot;
    bool ok;

    if (frame == NULL || len == 0 || (len + WIFI_TX_FCS_LEN) > WIFI_TX_BUFFER_SIZE) {
        ESP_LOGE(TAG, "TX invalid frame frame=%p len=%lu max=%u", frame, (unsigned long)len, WIFI_TX_BUFFER_SIZE);
        return false;
    }

    pf_sniff_stop();

    if (!wifi_tx_setup()) {
        return false;
    }

    if (!wifi_tx_apply_channel()) {
        return false;
    }

    slot = wifi_tx_find_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "TX no free slot");
        wifi_tx_log_all_slots("TX no-free-slot");
        return false;
    }

    s_tx_trace_sequence++;

    WIFI_TX_LOGI(TAG, "TX attempt trace=%lu slot=%d len=%lu channel=%u", (unsigned long)s_tx_trace_sequence, slot, (unsigned long)len, (unsigned)s_current_channel);

    if (!wifi_tx_prepare_slot((uint8_t)slot, frame, len)) {
        s_slots[slot].in_use = false;
        return false;
    }

    wifi_tx_program_slot((uint8_t)slot, len);
    ok = wifi_tx_wait_done((uint8_t)slot);
    s_slots[slot].in_use = false;

    if (!ok) {
        ESP_LOGE(TAG, "TX failed slot=%d len=%lu channel=%u", slot, (unsigned long)len, (unsigned)s_current_channel);
        return false;
    }

    WIFI_TX_LOGI(TAG, "TX done slot=%d len=%lu channel=%u", slot, (unsigned long)len, (unsigned)s_current_channel);
    return true;
}