idf_component_register(SRCS "main.c" "wifi.c"
                       PRIV_REQUIRES esp_wifi nvs_flash esp_ringbuf
                       INCLUDE_DIRS ".")
