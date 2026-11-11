idf_component_register(
    SRCS "main.c" "carelink_display.c"
    INCLUDE_DIRS "." "include"
    PRIV_REQUIRES carelink_board carelink_gps carelink_oled carelink_imu
                  carelink_incident carelink_security esp_timer nvs_flash
)
