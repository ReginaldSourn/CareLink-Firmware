idf_component_register(
    SRCS "carelink_buzzer.c" "carelink_buzzer_pattern.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES carelink_board carelink_button carelink_sos esp_driver_ledc
             freertos)
