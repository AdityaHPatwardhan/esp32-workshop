idf_component_register(
    SRCS "main.c" "config.c" "net.c" "camera.c" "console.c" "stream.c" "detector.cc"
         "model_settings.cc" "person_detect_model_data.cc"
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event console esp_driver_gpio
                  esp_psram esp_timer lwip esp_http_server
    INCLUDE_DIRS "")
