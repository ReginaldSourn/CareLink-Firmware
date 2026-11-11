#pragma once

#include <stdbool.h>
#include <stdint.h>

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
/* R2 only: initialize/read the switched GPIO1 battery divider. The returned
 * value is battery-terminal millivolts, not a fuel-gauge state of charge. */
esp_err_t carelink_board_battery_init(void);
esp_err_t carelink_board_battery_read_mv(uint32_t *battery_mv);
/* IMU-only validation; does not prevent GPS startup on an IMU config error. */
esp_err_t carelink_board_imu_validate(void);
int carelink_board_imu_sda_gpio(void);
int carelink_board_imu_scl_gpio(void);
bool carelink_board_imu_shared_bus(void);
bool carelink_board_imu_uses_vext(void);
