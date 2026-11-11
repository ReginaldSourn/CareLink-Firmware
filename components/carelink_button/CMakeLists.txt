idf_component_register(SRCS "carelink_button.c" "carelink_button_logic.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES carelink_board esp_driver_gpio esp_timer freertos)
