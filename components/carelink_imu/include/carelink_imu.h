#pragma once

#include "carelink_imu_state.h"
#include "esp_err.h"

/* Call from app_main before consumers; startup calls must be serialized.
 * Runs for firmware lifetime. Failed startup releases owned resources and can
 * be retried. Duplicate start returns ESP_ERR_INVALID_STATE. Disabled builds
 * return ESP_ERR_NOT_SUPPORTED. No stop/sleep API yet. */
esp_err_t carelink_imu_start(void);

/* Thread-safe after successful start. A successful copy does not imply a usable
 * sample: check state == CARELINK_IMU_READY. This is a latest-sample API, not a
 * lossless stream. Accel/gyro readings are not precisely time synchronized. */
esp_err_t carelink_imu_get_snapshot(carelink_imu_snapshot_t *snapshot);
