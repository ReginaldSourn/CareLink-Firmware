#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "carelink.h"
#include "carelink_board.h"
#include "carelink_gps.h"
#include "carelink_imu.h"
#include "carelink_display.h"

static const char *TAG = "carelink";

void app_main(void)
{
    printf("%s firmware starting\n", CARELINK_DEVICE_NAME);
    ESP_LOGI(TAG, "%s starting on %s", CARELINK_DEVICE_NAME, carelink_board_name());
    carelink_display_start();
    esp_err_t err = carelink_gps_start();
    bool gps_ready = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPS start failed: %s; check CareLink menuconfig", esp_err_to_name(err));
        carelink_display_gps_error();
    }
    err = carelink_imu_start();
    bool imu_ready = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "IMU start failed: %s; check CareLink board/IMU menuconfig",
                 esp_err_to_name(err));
    }

    while (true) {
        carelink_gps_snapshot_t gps;
        if (gps_ready && carelink_gps_get_snapshot(&gps) == ESP_OK) {
            carelink_display_gps(&gps);
            if (gps.state == CARELINK_GPS_FIXED) {
                ESP_LOGI(TAG, "GPS %s: lat=%.6f lon=%.6f age=%" PRId64 "ms",
                         gps.fix.sentence_type, gps.fix.latitude_deg, gps.fix.longitude_deg,
                         esp_timer_get_time() / 1000 - gps.fix_report_ms);
                if (gps.fix.has_altitude) ESP_LOGI(TAG, "altitude=%.1fm satellites=%u HDOP=%.1f",
                    gps.fix.altitude_m, gps.fix.satellites, gps.fix.has_hdop ? gps.fix.hdop : -1.0);
                if (gps.fix.has_speed) ESP_LOGI(TAG, "speed=%.2fm/s", gps.fix.speed_mps);
            } else {
                ESP_LOGI(TAG, "GPS %s: bytes=%" PRIu32 " accepted=%" PRIu32
                         " rejected=%" PRIu32 " UART errors=%" PRIu32,
                         carelink_gps_state_name(gps.state), gps.bytes_received,
                         gps.accepted_sentences, gps.rejected_sentences, gps.uart_errors);
            }
        }
        carelink_imu_snapshot_t imu;
        if (imu_ready && carelink_imu_get_snapshot(&imu) == ESP_OK) {
            if (imu.state == CARELINK_IMU_READY) {
                ESP_LOGI(TAG, "IMU accel[m/s2]=%.3f,%.3f,%.3f gyro[rad/s]=%.3f,%.3f,%.3f"
                         " seq=%" PRIu32 " age=%" PRId64 "ms errors=%" PRIu32,
                         imu.sample.acceleration_mps2[0], imu.sample.acceleration_mps2[1],
                         imu.sample.acceleration_mps2[2], imu.sample.angular_velocity_radps[0],
                         imu.sample.angular_velocity_radps[1], imu.sample.angular_velocity_radps[2],
                         imu.sequence, (esp_timer_get_time() - imu.sample_time_us) / 1000,
                         imu.read_errors);
            } else {
                ESP_LOGW(TAG, "IMU %s: errors=%" PRIu32,
                         carelink_imu_state_name(imu.state), imu.read_errors);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
