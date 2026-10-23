#include "lsm6dso_device.h"

carelink_lsm6dso_result_t carelink_lsm6dso_init(carelink_lsm6dso_t *device,
                                               const carelink_lsm6dso_config_t *config)
{
    if (!device) return CARELINK_LSM6DSO_INVALID_CONFIG;
    device->configured = false;
    if (!config || !device->ctx.read_reg || !device->ctx.write_reg ||
        !device->ctx.mdelay || !device->now_ms) return CARELINK_LSM6DSO_INVALID_CONFIG;
    lsm6dso_odr_xl_t xl_rate;
    lsm6dso_odr_g_t gy_rate;
    switch (config->rate_hz) {
    case 26: xl_rate = LSM6DSO_XL_ODR_26Hz; gy_rate = LSM6DSO_GY_ODR_26Hz; break;
    case 52: xl_rate = LSM6DSO_XL_ODR_52Hz; gy_rate = LSM6DSO_GY_ODR_52Hz; break;
    case 104: xl_rate = LSM6DSO_XL_ODR_104Hz; gy_rate = LSM6DSO_GY_ODR_104Hz; break;
    case 208: xl_rate = LSM6DSO_XL_ODR_208Hz; gy_rate = LSM6DSO_GY_ODR_208Hz; break;
    default: return CARELINK_LSM6DSO_INVALID_CONFIG;
    }
    lsm6dso_fs_xl_t xl_range;
    switch (config->accel_range_g) {
    case 2: xl_range = LSM6DSO_2g; device->accel_to_mg = lsm6dso_from_fs2_to_mg; break;
    case 4: xl_range = LSM6DSO_4g; device->accel_to_mg = lsm6dso_from_fs4_to_mg; break;
    case 8: xl_range = LSM6DSO_8g; device->accel_to_mg = lsm6dso_from_fs8_to_mg; break;
    case 16: xl_range = LSM6DSO_16g; device->accel_to_mg = lsm6dso_from_fs16_to_mg; break;
    default: return CARELINK_LSM6DSO_INVALID_CONFIG;
    }
    lsm6dso_fs_g_t gy_range;
    switch (config->gyro_range_dps) {
    case 250: gy_range = LSM6DSO_250dps; device->gyro_to_mdps = lsm6dso_from_fs250_to_mdps; break;
    case 500: gy_range = LSM6DSO_500dps; device->gyro_to_mdps = lsm6dso_from_fs500_to_mdps; break;
    case 1000: gy_range = LSM6DSO_1000dps; device->gyro_to_mdps = lsm6dso_from_fs1000_to_mdps; break;
    case 2000: gy_range = LSM6DSO_2000dps; device->gyro_to_mdps = lsm6dso_from_fs2000_to_mdps; break;
    default: return CARELINK_LSM6DSO_INVALID_CONFIG;
    }
    const stmdev_ctx_t *ctx = &device->ctx;
    uint8_t id = 0;
    if (lsm6dso_device_id_get(ctx, &id)) return CARELINK_LSM6DSO_IO_ERROR;
    if (id != LSM6DSO_ID) return CARELINK_LSM6DSO_WRONG_ID;
    if (lsm6dso_reset_set(ctx, 1)) return CARELINK_LSM6DSO_IO_ERROR;
    int64_t deadline = device->now_ms(ctx->handle) + 200;
    uint8_t resetting = 1;
    while (resetting) {
        if (device->now_ms(ctx->handle) >= deadline) return CARELINK_LSM6DSO_RESET_TIMEOUT;
        ctx->mdelay(5);
        if (lsm6dso_reset_get(ctx, &resetting)) return CARELINK_LSM6DSO_IO_ERROR;
    }
    /* Configure while powered down; enable ODRs last. */
    if (lsm6dso_i3c_disable_set(ctx, LSM6DSO_I3C_DISABLE) ||
        lsm6dso_auto_increment_set(ctx, 1) ||
        lsm6dso_block_data_update_set(ctx, 1) ||
        lsm6dso_xl_full_scale_set(ctx, xl_range) ||
        lsm6dso_gy_full_scale_set(ctx, gy_range) ||
        lsm6dso_xl_data_rate_set(ctx, xl_rate) ||
        lsm6dso_gy_data_rate_set(ctx, gy_rate)) {
        carelink_lsm6dso_power_down(device);
        return CARELINK_LSM6DSO_IO_ERROR;
    }
    /* Allow gyro settling before the first published sample. */
    ctx->mdelay(100);
    device->configured = true;
    return CARELINK_LSM6DSO_OK;
}

static int16_t decode_le(const uint8_t *bytes)
{
    int32_t value = (int32_t)bytes[0] | ((int32_t)bytes[1] << 8);
    if (value >= 32768) value -= 65536;
    return (int16_t)value;
}

carelink_lsm6dso_result_t carelink_lsm6dso_read(carelink_lsm6dso_t *device,
                                               carelink_imu_sample_t *sample)
{
    if (!device || !sample || !device->configured) return CARELINK_LSM6DSO_INVALID_CONFIG;
    lsm6dso_status_reg_t status = {0};
    if (lsm6dso_status_reg_get(&device->ctx, &status)) return CARELINK_LSM6DSO_IO_ERROR;
    if (!status.xlda || !status.gda) return CARELINK_LSM6DSO_NO_SAMPLE;
    uint8_t raw[12];
    if (lsm6dso_read_reg(&device->ctx, LSM6DSO_OUTX_L_G, raw, sizeof(raw)))
        return CARELINK_LSM6DSO_IO_ERROR;
    carelink_imu_sample_t result;
    for (unsigned axis = 0; axis < 3; ++axis) {
        result.angular_velocity_radps[axis] =
            device->gyro_to_mdps(decode_le(raw + axis * 2)) * 0.0000174532925199433f;
        result.acceleration_mps2[axis] =
            device->accel_to_mg(decode_le(raw + 6 + axis * 2)) * 0.00980665f;
    }
    *sample = result;
    return CARELINK_LSM6DSO_OK;
}

void carelink_lsm6dso_power_down(carelink_lsm6dso_t *device)
{
    if (!device) return;
    device->configured = false;
    /* Best effort during failure cleanup; transport has finite timeouts. */
    (void)lsm6dso_xl_data_rate_set(&device->ctx, LSM6DSO_XL_ODR_OFF);
    (void)lsm6dso_gy_data_rate_set(&device->ctx, LSM6DSO_GY_ODR_OFF);
}
