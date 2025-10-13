idf_component_register(SRCS "main.c" "wifi.c" "discovery_server.c" "rtp_server.c"
                       PRIV_REQUIRES esp_wifi nvs_flash esp_ringbuf
                       INCLUDE_DIRS ".")
