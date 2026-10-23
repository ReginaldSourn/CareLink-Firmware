#include <stdbool.h>

#include "carelink_board.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "Heltec WiFi LoRa 32 V4 requires idf.py set-target esp32s3"
#endif

#if CONFIG_CARELINK_BOARD_HELTEC_V4_R8
#define GNSS_POWER_GPIO 42
#define VEXT_POWER_GPIO 40
#define BOARD_NAME "Heltec WiFi LoRa 32 V4 R8"
#else
#define GNSS_POWER_GPIO 34
#define VEXT_POWER_GPIO 36
#define BOARD_NAME "Heltec WiFi LoRa 32 V4 R2"
#endif

const char *carelink_board_name(void) { return BOARD_NAME; }
int carelink_board_gps_rx_gpio(void) { return CONFIG_CARELINK_GPS_RX_GPIO; }
int carelink_board_gps_baud(void) { return CONFIG_CARELINK_GPS_BAUD; }

esp_err_t carelink_board_vext_power(bool enabled)
{
    esp_err_t err = gpio_set_level(VEXT_POWER_GPIO, enabled ? 0 : 1);
    if (err != ESP_OK) return err;
    return gpio_set_direction(VEXT_POWER_GPIO, GPIO_MODE_OUTPUT);
}

esp_err_t carelink_board_validate(void)
{
    /* Free header pins common to both variants; avoid flash/PSRAM, USB,
     * strapping, radio, OLED and power controls, even when PSRAM is disabled. */
    switch (CONFIG_CARELINK_GPS_RX_GPIO) {
    case 4: case 6: case 15: case 16: case 38: case 39: case 47: case 48:
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t carelink_board_gnss_power(bool enabled)
{
#if CONFIG_CARELINK_GNSS_SOCKET_POWER
    /* Preload the latch before enabling the output to avoid a power glitch. */
    esp_err_t err = gpio_set_level(GNSS_POWER_GPIO, enabled ? 0 : 1);
    if (err != ESP_OK) return err;
    return gpio_set_direction(GNSS_POWER_GPIO, GPIO_MODE_OUTPUT);
#else
    (void)enabled;
    return ESP_OK;
#endif
}

bool carelink_board_imu_shared_bus(void)
{
#if CONFIG_CARELINK_IMU_SHARED_I2C
    return true;
#else
    return false;
#endif
}

bool carelink_board_imu_uses_vext(void)
{
#if CONFIG_CARELINK_IMU_VEXT_POWER
    return true;
#else
    return false;
#endif
}

int carelink_board_imu_sda_gpio(void)
{
#if CONFIG_CARELINK_IMU_SHARED_I2C
    return CARELINK_OLED_SDA_GPIO;
#elif CONFIG_CARELINK_IMU_ENABLED
    return CONFIG_CARELINK_IMU_SDA_GPIO;
#else
    return -1;
#endif
}

int carelink_board_imu_scl_gpio(void)
{
#if CONFIG_CARELINK_IMU_SHARED_I2C
    return CARELINK_OLED_SCL_GPIO;
#elif CONFIG_CARELINK_IMU_ENABLED
    return CONFIG_CARELINK_IMU_SCL_GPIO;
#else
    return -1;
#endif
}

static bool imu_header_pin_available(int pin)
{
    if (pin == carelink_board_gps_rx_gpio()) return false;
    switch (pin) {
    case 4: case 6: case 15: case 16: case 38: case 39: case 47: case 48:
        return true;
    default:
        return false;
    }
}

esp_err_t carelink_board_imu_validate(void)
{
    int sda = carelink_board_imu_sda_gpio();
    int scl = carelink_board_imu_scl_gpio();
    if (sda == scl) return ESP_ERR_INVALID_ARG;
    if (carelink_board_imu_shared_bus()) {
        if (sda == carelink_board_gps_rx_gpio() || scl == carelink_board_gps_rx_gpio())
            return ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }
    return imu_header_pin_available(sda) && imu_header_pin_available(scl)
        ? ESP_OK : ESP_ERR_INVALID_ARG;
}
