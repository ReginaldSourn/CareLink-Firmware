idf_component_register(SRCS "carelink_gps.c" "carelink_gps_state.c"
                       INCLUDE_DIRS "include"
                       REQUIRES carelink_nmea
                       PRIV_REQUIRES carelink_board esp_driver_uart esp_timer)
