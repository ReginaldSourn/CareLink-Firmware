#pragma once

#include "carelink_imu_state.h"
#include "lsm6dso_reg.h"

typedef enum {
    CARELINK_LSM6DSO_OK,
    CARELINK_LSM6DSO_NO_SAMPLE,
    CARELINK_LSM6DSO_IO_ERROR,
    CARELINK_LSM6DSO_WRONG_ID,
    CARELINK_LSM6DSO_RESET_TIMEOUT,
    CARELINK_LSM6DSO_INVALID_CONFIG,
} carelink_lsm6dso_result_t;

typedef struct {
    unsigned rate_hz;       /* 26, 52, 104, 208 */
    unsigned accel_range_g; /* 2, 4, 8, 16 */
    unsigned gyro_range_dps;/* 250, 500, 1000, 2000 */
} carelink_lsm6dso_config_t;

typedef struct {
    stmdev_ctx_t ctx;
    int64_t (*now_ms)(void *handle);
    float_t (*accel_to_mg)(int16_t);
    float_t (*gyro_to_mdps)(int16_t);
    bool configured;
} carelink_lsm6dso_t;

carelink_lsm6dso_result_t carelink_lsm6dso_init(carelink_lsm6dso_t *device,
                                               const carelink_lsm6dso_config_t *config);
/* Output is unchanged on NO_SAMPLE or error. */
carelink_lsm6dso_result_t carelink_lsm6dso_read(carelink_lsm6dso_t *device,
                                               carelink_imu_sample_t *sample);
void carelink_lsm6dso_power_down(carelink_lsm6dso_t *device);
