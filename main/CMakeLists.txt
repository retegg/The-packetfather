idf_component_register(
    SRCS
        "main.c"
        "pf.c"
        "packet.c"
        "network.c"
        "tx.c"
        "wifi_power.c"
        "wifi_dma.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        esp_phy
        nvs_flash

    PRIV_REQUIRES
        esp_hw_support
)
