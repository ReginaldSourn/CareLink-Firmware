#include <stdbool.h>

#include "carelink_board.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#if CONFIG_CARELINK_BOARD_HELTEC_V4_R2
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

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
#define VBAT_ADC_CTRL_GPIO 37
#define VBAT_ADC_UNIT ADC_UNIT_1
#define VBAT_ADC_CHANNEL ADC_CHANNEL_0
#define VBAT_ADC_ATTEN ADC_ATTEN_DB_2_5
#define VBAT_SAMPLE_COUNT 16
#define VBAT_DIVIDER_NUMERATOR 490
#define VBAT_DIVIDER_DENOMINATOR 100
#endif

#if CONFIG_CARELINK_BOARD_HELTEC_V4_R2
static adc_oneshot_unit_handle_t vbat_adc;
static adc_cali_handle_t vbat_calibration;
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

esp_err_t carelink_board_battery_init(void)
{
#if CONFIG_CARELINK_BOARD_HELTEC_V4_R2
    if (vbat_adc) return ESP_ERR_INVALID_STATE;

    /* Keep the P-channel switch off except while sampling. GPIO37 drives Q6,
     * so HIGH enables Q5 and connects VBAT to the 390K/100K divider. */
    esp_err_t err = gpio_set_level(VBAT_ADC_CTRL_GPIO, 0);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(VBAT_ADC_CTRL_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) return err;

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = VBAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&unit_config, &vbat_adc);
    if (err != ESP_OK) return err;

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = VBAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(vbat_adc, VBAT_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) goto fail;

    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = VBAT_ADC_UNIT,
        .chan = VBAT_ADC_CHANNEL,
        .atten = VBAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&calibration_config, &vbat_calibration);
    if (err != ESP_OK) goto fail;
    return ESP_OK;

fail:
    adc_oneshot_del_unit(vbat_adc);
    vbat_adc = NULL;
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t carelink_board_battery_read_mv(uint32_t *battery_mv)
{
    if (!battery_mv) return ESP_ERR_INVALID_ARG;
#if CONFIG_CARELINK_BOARD_HELTEC_V4_R2
    if (!vbat_adc || !vbat_calibration) return ESP_ERR_INVALID_STATE;

    esp_err_t err = gpio_set_level(VBAT_ADC_CTRL_GPIO, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    uint32_t raw_sum = 0;
    for (unsigned int i = 0; i < VBAT_SAMPLE_COUNT; ++i) {
        int raw = 0;
        err = adc_oneshot_read(vbat_adc, VBAT_ADC_CHANNEL, &raw);
        if (err != ESP_OK) goto disable_divider;
        raw_sum += (uint32_t)raw;
        esp_rom_delay_us(200);
    }

    int adc_mv = 0;
    err = adc_cali_raw_to_voltage(vbat_calibration,
                                  (int)(raw_sum / VBAT_SAMPLE_COUNT), &adc_mv);
    if (err == ESP_OK) {
        /* ADC_IN = VBAT * 100K / (390K + 100K), hence VBAT = ADC_IN * 4.9. */
        *battery_mv = ((uint32_t)adc_mv * VBAT_DIVIDER_NUMERATOR
                       + VBAT_DIVIDER_DENOMINATOR / 2)
                    / VBAT_DIVIDER_DENOMINATOR;
    }

disable_divider:
    {
        esp_err_t disable_err = gpio_set_level(VBAT_ADC_CTRL_GPIO, 0);
        return err == ESP_OK ? disable_err : err;
    }
#else
    return ESP_ERR_NOT_SUPPORTED;
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
