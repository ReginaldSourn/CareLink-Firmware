#include "carelink_imu.h"
#include "sdkconfig.h"

#if CONFIG_CARELINK_IMU_ENABLED
#include "carelink_board.h"
#include "lsm6dso_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static carelink_imu_snapshot_t shared;
static carelink_lsm6dso_port_t port;
static carelink_lsm6dso_t device;
static bool started;
static const carelink_lsm6dso_config_t config = {
    .rate_hz = CONFIG_CARELINK_IMU_RATE_HZ,
    .accel_range_g = CONFIG_CARELINK_IMU_ACCEL_RANGE_G,
    .gyro_range_dps = CONFIG_CARELINK_IMU_GYRO_RANGE_DPS,
};

static void publish(const carelink_imu_snapshot_t *snapshot)
{
    taskENTER_CRITICAL(&lock);
    shared = *snapshot;
    taskEXIT_CRITICAL(&lock);
}

static void imu_task(void *arg)
{
    (void)arg;
    carelink_imu_snapshot_t current = {.sample_time_us = -1};
    int64_t progress_us = esp_timer_get_time();
    bool reinitialize = false;
    /* Poll at least once per RTOS tick, targeting twice the sensor ODR. A
     * latest-sample consumer may skip conversions at high ODR or under load. */
    TickType_t poll_ticks = pdMS_TO_TICKS(500 / CONFIG_CARELINK_IMU_RATE_HZ);
    if (!poll_ticks) poll_ticks = 1;
    for (;;) {
        if (reinitialize) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            carelink_lsm6dso_result_t result = carelink_lsm6dso_init(&device, &config);
            if (result != CARELINK_LSM6DSO_OK) {
                current.fault = true;
                ++current.read_errors;
                publish(&current);
                continue;
            }
            /* Preserve error/stale status until a new complete sample arrives. */
            progress_us = esp_timer_get_time();
            reinitialize = false;
        }
        vTaskDelay(poll_ticks);
        carelink_imu_sample_t sample;
        carelink_lsm6dso_result_t result = carelink_lsm6dso_read(&device, &sample);
        int64_t now = esp_timer_get_time();
        if (result == CARELINK_LSM6DSO_OK) {
            current.sample = sample;
            current.sample_time_us = now;
            ++current.sequence;
            current.fault = false;
            progress_us = now;
        } else if (result != CARELINK_LSM6DSO_NO_SAMPLE) {
            current.fault = true;
            ++current.read_errors;
            reinitialize = true;
        } else if (now - progress_us >= (int64_t)CONFIG_CARELINK_IMU_STALE_MS * 1000) {
            /* Also recover a sensor that acknowledged I2C but lost its ODR
             * settings after a supply interruption. */
            reinitialize = true;
        }
        publish(&current);
    }
}

esp_err_t carelink_imu_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    esp_err_t err = carelink_lsm6dso_port_open(&port, &device, CONFIG_CARELINK_IMU_I2C_ADDRESS);
    if (err != ESP_OK) return err;
    carelink_lsm6dso_result_t result = carelink_lsm6dso_init(&device, &config);
    if (result != CARELINK_LSM6DSO_OK) {
        ESP_LOGE(TAG, "LSM6DSO initialization failed (reason=%d, I2C=%s)",
                 result, esp_err_to_name(port.last_error));
        if (result == CARELINK_LSM6DSO_WRONG_ID) err = ESP_ERR_INVALID_RESPONSE;
        else if (result == CARELINK_LSM6DSO_RESET_TIMEOUT) err = ESP_ERR_TIMEOUT;
        else err = port.last_error != ESP_OK ? port.last_error : ESP_FAIL;
        goto fail;
    }
    shared = (carelink_imu_snapshot_t){.sample_time_us = -1};
    if (xTaskCreate(imu_task, "carelink_imu", 4096, NULL, 5, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        carelink_lsm6dso_power_down(&device);
        goto fail;
    }
    started = true;
    ESP_LOGI(TAG, "LSM6DSO SDA=%d SCL=%d addr=0x%02x, %u Hz, +/-%u g, +/-%u dps",
             carelink_board_imu_sda_gpio(), carelink_board_imu_scl_gpio(),
             CONFIG_CARELINK_IMU_I2C_ADDRESS, config.rate_hz,
             config.accel_range_g, config.gyro_range_dps);
    return ESP_OK;
fail:
    carelink_lsm6dso_port_close(&port);
    device = (carelink_lsm6dso_t){0};
    return err;
}

esp_err_t carelink_imu_get_snapshot(carelink_imu_snapshot_t *snapshot)
{
    if (!snapshot) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&lock);
    *snapshot = shared;
    taskEXIT_CRITICAL(&lock);
    snapshot->state = carelink_imu_classify(snapshot, esp_timer_get_time(),
                                           (int64_t)CONFIG_CARELINK_IMU_STALE_MS * 1000);
    return ESP_OK;
}
#else
esp_err_t carelink_imu_start(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t carelink_imu_get_snapshot(carelink_imu_snapshot_t *snapshot)
{
    (void)snapshot;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
