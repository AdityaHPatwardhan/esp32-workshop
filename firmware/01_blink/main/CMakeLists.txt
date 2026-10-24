idf_component_register(SRCS "blink_main.c" "board_report.c" "hello_net.c" "console.c"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2s spi_flash esp_hw_support esp_psram
                                     esp_partition nvs_flash esp_timer heap esp_wifi esp_netif esp_event console lwip
                       INCLUDE_DIRS "")
