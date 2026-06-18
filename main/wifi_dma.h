#pragma once

/*
 * RX DMA data structures and control entry points.
 *
 * The layout is experimental and based on observed ESP32-C3 Wi-Fi DMA behavior.
 */

#include <stdbool.h>
#include <stdint.h>

#define WIFI_RX_BUFFER_COUNT 10
#define WIFI_RX_BUFFER_SIZE 1600

typedef struct {
    signed rssi : 8;

    unsigned rate : 5;
    unsigned reserved_0 : 1;
    unsigned sig_mode : 2;

    unsigned reserved_1 : 8;

    unsigned reserved_2 : 4;
    unsigned filter_match : 2;
    unsigned reserved_3 : 2;

    unsigned mcs : 7;
    unsigned cwb : 1;

    unsigned reserved_4 : 16;

    unsigned smoothing : 1;
    unsigned not_sounding : 1;
    unsigned reserved_5 : 1;
    unsigned aggregation : 1;
    unsigned stbc : 2;
    unsigned fec_coding : 1;
    unsigned sgi : 1;

    signed noise_floor : 8;

    unsigned ampdu_cnt : 8;

    unsigned channel : 4;
    unsigned secondary_channel : 4;

    unsigned reserved_6 : 8;

    unsigned timestamp : 32;

    unsigned reserved_7 : 32;

    unsigned reserved_8 : 31;
    unsigned ant : 1;

    unsigned sig_len : 12;
    unsigned reserved_9 : 12;
    unsigned rx_state : 8;
} wifi_rx_ctrl_t;

typedef struct {
    wifi_rx_ctrl_t rx_ctrl;
    uint8_t payload[0];
} wifi_rx_packet_t;

typedef struct __attribute__((packed, aligned(4))) wifi_dma_desc {
    uint16_t size : 12;
    uint16_t length : 12;

    uint8_t unknown : 6;
    uint8_t has_data : 1;
    uint8_t owner : 1;

    wifi_rx_packet_t *packet;
    struct wifi_dma_desc *next;
} wifi_dma_desc_t;

void wifi_dma_rx_setup(void);
void wifi_dma_rx_attach_to_hardware(void);
void wifi_dma_rx_kick_hardware(void);
bool wifi_dma_rx_handle_ready(void);
void wifi_dma_rx_poll_task(void *arg);
bool wifi_dma_rx_start_polling(void);
void wifi_dma_rx_stop_polling(void);
void wifi_dma_dump_registers(const char *label);
