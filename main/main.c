#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "carelink.h"
#include "carelink_board.h"
#include "carelink_ble_config.h"
#include "carelink_buzzer.h"
#include "carelink_config.h"
#include "carelink_gps.h"
#include "carelink_imu.h"
#include "carelink_display.h"
#include "carelink_incident.h"
#include "carelink_security.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "carelink";

#if CONFIG_CARELINK_INCIDENT_ENABLED
typedef struct {
    carelink_security_t security;
    portMUX_TYPE telemetry_lock;
    uint16_t battery_mv;
    bool battery_valid;
    bool gps_ready;
} app_incident_context_t;

static app_incident_context_t incident_context = {
    .telemetry_lock = portMUX_INITIALIZER_UNLOCKED,
};

static void update_incident_battery(bool valid, uint32_t battery_mv)
{
    portENTER_CRITICAL(&incident_context.telemetry_lock);
    incident_context.battery_valid = valid;
    incident_context.battery_mv =
        battery_mv > UINT16_MAX ? UINT16_MAX : (uint16_t)battery_mv;
    portEXIT_CRITICAL(&incident_context.telemetry_lock);
}

static esp_err_t get_incident_telemetry(
    void *context,
    carelink_incident_telemetry_t *telemetry)
{
    app_incident_context_t *app = (app_incident_context_t *)context;
    if (app == NULL || telemetry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *telemetry = (carelink_incident_telemetry_t){
        .gnss = app->gps_ready ? CARELINK_GNSS_SEARCHING
                               : CARELINK_GNSS_UNAVAILABLE,
        .location_age_s = CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
    };
    portENTER_CRITICAL(&app->telemetry_lock);
    const bool battery_valid = app->battery_valid;
    const uint16_t battery_mv = app->battery_mv;
    portEXIT_CRITICAL(&app->telemetry_lock);
    if (battery_valid) {
        telemetry->battery_mv = battery_mv;
    }
    if (!app->gps_ready) {
        return ESP_OK;
    }

    carelink_gps_snapshot_t gps;
    const esp_err_t error = carelink_gps_get_snapshot(&gps);
    if (error != ESP_OK) {
        return error;
    }
    if (gps.state != CARELINK_GPS_FIXED &&
        gps.state != CARELINK_GPS_STALE) {
        telemetry->gnss = CARELINK_GNSS_SEARCHING;
        return ESP_OK;
    }
    telemetry->gnss = gps.state == CARELINK_GPS_FIXED
                          ? CARELINK_GNSS_FRESH
                          : CARELINK_GNSS_STALE;
    telemetry->has_location = true;
    telemetry->latitude_e7 = (int32_t)(gps.fix.latitude_deg * 10000000.0);
    telemetry->longitude_e7 = (int32_t)(gps.fix.longitude_deg * 10000000.0);
    int64_t age_ms = esp_timer_get_time() / 1000 - gps.fix_report_ms;
    if (age_ms < 0) {
        age_ms = 0;
    }
    const uint64_t age_s = (uint64_t)age_ms / 1000U;
    telemetry->location_age_s =
        age_s >= CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN
            ? CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN - 1U
            : (uint32_t)age_s;
    return ESP_OK;
}

static esp_err_t next_incident_counter(void *context, uint64_t *counter)
{
    app_incident_context_t *app = (app_incident_context_t *)context;
    return app == NULL
               ? ESP_ERR_INVALID_ARG
               : carelink_security_next_replay_counter(&app->security,
                                                       counter);
}

#if CONFIG_CARELINK_BUZZER_ENABLED
static void incident_status_changed(carelink_sos_state_t state,
                                    bool cancellation_pending,
                                    void *context)
{
    (void)context;
    const esp_err_t error =
        carelink_buzzer_notify_sos(state, cancellation_pending);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Buzzer state update failed: %s",
                 esp_err_to_name(error));
    }
}
#endif
#endif

void app_main(void)
{
    printf("%s firmware starting\n", CARELINK_DEVICE_NAME);
    ESP_LOGI(TAG, "%s starting on %s", CARELINK_DEVICE_NAME, carelink_board_name());

    carelink_runtime_config_t runtime_config;
    carelink_config_defaults(&runtime_config);
    bool nvs_ready = false;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) {
        nvs_ready = true;
        carelink_config_source_t config_source;
        err = carelink_config_load(&runtime_config, &config_source);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Runtime configuration load failed: %s; using defaults",
                     esp_err_to_name(err));
            carelink_config_defaults(&runtime_config);
        } else if (config_source == CARELINK_CONFIG_SOURCE_RECOVERY) {
            ESP_LOGW(TAG, "Runtime configuration is corrupt; using safe defaults");
        } else if (config_source == CARELINK_CONFIG_SOURCE_RUNTIME) {
            ESP_LOGI(TAG, "Applied mobile runtime configuration");
        }
    } else {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
    }
#if !CONFIG_CARELINK_INCIDENT_ENABLED && !CONFIG_CARELINK_BLE_CONFIG_ENABLED
    (void)nvs_ready;
#endif

    esp_err_t battery_err = carelink_board_battery_init();
    bool battery_ready = battery_err == ESP_OK;
    if (battery_err != ESP_OK && battery_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Battery monitor unavailable: %s", esp_err_to_name(battery_err));
    }
    if (runtime_config.oled_enabled) {
        carelink_display_start();
    }
    err = carelink_gps_start_with_stale_ms(runtime_config.gps_stale_ms);
    bool gps_ready = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPS start failed: %s; check CareLink menuconfig", esp_err_to_name(err));
    }
    err = carelink_imu_start();
    bool imu_ready = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "IMU start failed: %s; check CareLink board/IMU menuconfig",
                 esp_err_to_name(err));
    }

    bool incident_ready = false;
#if CONFIG_CARELINK_INCIDENT_ENABLED
#if CONFIG_CARELINK_BUZZER_ENABLED
    const esp_err_t buzzer_error = carelink_buzzer_start();
    const bool buzzer_ready = buzzer_error == ESP_OK;
    if (!buzzer_ready) {
        ESP_LOGE(TAG, "SOS buzzer unavailable: %s; check GPIO and configuration",
                 esp_err_to_name(buzzer_error));
    }
#endif
    incident_context.gps_ready = gps_ready;
    uint32_t initial_battery_mv = 0U;
    const bool initial_battery_available =
        battery_ready &&
        carelink_board_battery_read_mv(&initial_battery_mv) == ESP_OK;
    update_incident_battery(initial_battery_available, initial_battery_mv);
    err = nvs_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        err = carelink_security_init(&incident_context.security);
    }
    uint64_t device_id = 0U;
    if (err == ESP_OK) {
        err = carelink_security_device_id(&device_id);
    }
    if (err == ESP_OK) {
        const carelink_incident_config_t incident_config = {
            .device_id = device_id,
            .session_id = carelink_security_new_session_id(),
            .key_provider = carelink_security_key_provider(
                &incident_context.security),
            .get_telemetry = get_incident_telemetry,
            .next_replay_counter = next_incident_counter,
#if CONFIG_CARELINK_BUZZER_ENABLED
            .status_changed = buzzer_ready ? incident_status_changed : NULL,
#endif
            .radio = {
                .frequency_hz = runtime_config.radio_frequency_hz,
                .tx_power_dbm = runtime_config.radio_tx_power_dbm,
            },
            .retry = {
                .first_delay_ms = runtime_config.retry_first_ms,
                .maximum_delay_ms = runtime_config.retry_max_ms,
                .maximum_attempts = runtime_config.maximum_attempts,
            },
            .location_update_ms = runtime_config.location_update_ms,
            .context = &incident_context,
        };
        err = carelink_incident_start(&incident_config);
    }
    if (err == ESP_OK) {
        incident_ready = true;
        ESP_LOGI(TAG, "authenticated SOS incident service ready");
    } else {
        ESP_LOGE(TAG, "SOS incident service unavailable: %s",
                 esp_err_to_name(err));
    }
#endif

#if CONFIG_CARELINK_BLE_CONFIG_ENABLED
    if (nvs_ready) {
        const esp_err_t ble_error = carelink_ble_config_start();
        if (ble_error != ESP_OK) {
            ESP_LOGE(TAG, "BLE mobile configuration unavailable: %s",
                     esp_err_to_name(ble_error));
        }
    }
#endif

    while (true) {
        uint32_t battery_mv = 0U;
        bool battery_sample_available = false;
        if (battery_ready) {
            battery_err = carelink_board_battery_read_mv(&battery_mv);
            if (battery_err == ESP_OK) {
                battery_sample_available = true;
                ESP_LOGI(TAG, "Battery: %.3f V", (double)battery_mv / 1000.0);
            } else {
                ESP_LOGW(TAG, "Battery read failed: %s", esp_err_to_name(battery_err));
            }
        }
#if CONFIG_CARELINK_INCIDENT_ENABLED
        update_incident_battery(battery_sample_available, battery_mv);
#endif

        carelink_gps_snapshot_t gps = {0};
        const bool gps_sample_available =
            gps_ready && carelink_gps_get_snapshot(&gps) == ESP_OK;
        if (gps_sample_available) {
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
        carelink_incident_status_t incident = {0};
        if (incident_ready) {
            incident = carelink_incident_get_status();
        }
        if (runtime_config.oled_enabled) {
            const carelink_display_status_t display_status = {
                .gps = gps,
                .incident = incident,
                .battery_mv = battery_mv,
                .gps_available = gps_sample_available,
                .incident_available = incident_ready,
                .battery_available = battery_sample_available,
            };
            carelink_display_update(&display_status);
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
