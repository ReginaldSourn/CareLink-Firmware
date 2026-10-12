#pragma once

#include <stdbool.h>

#include "esp_err.h"

const char *carelink_board_name(void);
int carelink_board_gps_rx_gpio(void);
int carelink_board_gps_baud(void);
enum {
    CARELINK_OLED_SDA_GPIO = 17,
    CARELINK_OLED_SCL_GPIO = 18,
    CARELINK_OLED_RESET_GPIO = 21,
    CARELINK_OLED_I2C_ADDRESS = 0x3c,
};
/* Vext is shared with the external supply headers; keep it on while in use. */
esp_err_t carelink_board_vext_power(bool enabled);
esp_err_t carelink_board_gnss_power(bool enabled);
esp_err_t carelink_board_validate(void);
