idf_component_register(
    SRCS
        "main.c"
        "wifi_power.c"
        "wifi_dma.c"
        "wifi_parser.c"
        "wifi_mac.c"
        "wifi_probe.c"
        "wifi_regdump.c"
        "wifi_rx_analyzer.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_phy
        nvs_flash

    PRIV_REQUIRES
        esp_hw_support
)
