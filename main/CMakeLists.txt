idf_component_register(
    SRCS "mqtt.cpp" "ir_control.cpp" "main.cpp" "wifi.c" "ac_state.cpp"
    INCLUDE_DIRS "." "include"
    PRIV_REQUIRES driver esp_wifi esp_event nvs_flash mqtt 
    EMBED_TXTFILES "certs/isrgrootx1.pem"
    REQUIRES mbedtls freertos esp_timer
)
