#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CARELINK_IMU_NO_DATA,
    CARELINK_IMU_READY,
    CARELINK_IMU_STALE,
    CARELINK_IMU_ERROR,
} carelink_imu_state_t;

typedef struct {
    float acceleration_mps2[3];
    float angular_velocity_radps[3];
} carelink_imu_sample_t;

typedef struct {
    carelink_imu_sample_t sample; /* Sensor-frame X/Y/Z; usable only when READY. */
    carelink_imu_state_t state;
    int64_t sample_time_us;      /* Monotonic reception time; -1 before first sample. */
    uint32_t sequence;           /* Wraps naturally; counts published samples. */
    uint32_t read_errors;
    bool fault;
} carelink_imu_snapshot_t;

carelink_imu_state_t carelink_imu_classify(const carelink_imu_snapshot_t *snapshot,
                                         int64_t now_us, int64_t stale_us);
const char *carelink_imu_state_name(carelink_imu_state_t state);
