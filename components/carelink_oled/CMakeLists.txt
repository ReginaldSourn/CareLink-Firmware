idf_component_register(SRCS "carelink_oled.c" "oled_text.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES carelink_board carelink_i2c esp_driver_i2c esp_lcd)
