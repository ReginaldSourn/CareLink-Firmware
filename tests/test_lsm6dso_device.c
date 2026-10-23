#include <assert.h>
#include <math.h>
#include <string.h>
#include "lsm6dso_device.h"

typedef struct {
    uint8_t regs[256];
    int64_t time_ms;
    unsigned transactions;
    unsigned writes;
    unsigned fail_at;
    bool stuck_reset;
} fake_t;

static fake_t fake;

static int32_t read_reg(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    fake_t *f = handle;
    assert((unsigned)reg + len <= sizeof(f->regs));
    if (++f->transactions == f->fail_at) {
        /* A driver must not publish even partially overwritten output. */
        if (len) data[0] = 0xff;
        return -1;
    }
    if (reg == LSM6DSO_CTRL3_C && (f->regs[reg] & 1) && !f->stuck_reset) {
        uint8_t id = f->regs[LSM6DSO_WHO_AM_I];
        memset(f->regs, 0, sizeof(f->regs));
        f->regs[LSM6DSO_WHO_AM_I] = id;
    }
    memcpy(data, f->regs + reg, len);
    return 0;
}

static int32_t write_reg(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    fake_t *f = handle;
    assert((unsigned)reg + len <= sizeof(f->regs));
    if (++f->transactions == f->fail_at) return -1;
    ++f->writes;
    memcpy(f->regs + reg, data, len);
    return 0;
}

static void delay_ms(uint32_t ms) { fake.time_ms += ms; }
static int64_t now_ms(void *handle) { return ((fake_t *)handle)->time_ms; }

static carelink_lsm6dso_t new_device(void)
{
    fake = (fake_t){0};
    fake.regs[LSM6DSO_WHO_AM_I] = LSM6DSO_ID;
    return (carelink_lsm6dso_t) {
        .ctx = {.read_reg = read_reg, .write_reg = write_reg,
                .mdelay = delay_ms, .handle = &fake},
        .now_ms = now_ms,
    };
}

static void raw_word(unsigned offset, int16_t value)
{
    uint16_t bits = (uint16_t)value;
    fake.regs[offset] = bits & 0xff;
    fake.regs[offset + 1] = bits >> 8;
}

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < fmaxf(0.00001f, fabsf(expected) * 0.00001f));
}

int main(void)
{
    const carelink_lsm6dso_config_t config = {104, 4, 500};
    carelink_lsm6dso_t device = new_device();
    assert(carelink_lsm6dso_init(&device, &config) == CARELINK_LSM6DSO_OK);
    unsigned init_transactions = fake.transactions;
    assert((fake.regs[LSM6DSO_CTRL3_C] & 0x44) == 0x44); /* BDU and IF_INC */
    assert(fake.regs[LSM6DSO_CTRL1_XL] == 0x48); /* 104 Hz, 4g */
    assert(fake.regs[LSM6DSO_CTRL2_G] == 0x44);  /* 104 Hz, 500 dps */
    assert(fake.time_ms >= 100);

    carelink_imu_sample_t output = {{1, 2, 3}, {4, 5, 6}};
    carelink_imu_sample_t sentinel = output;
    assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_NO_SAMPLE);
    assert(memcmp(&output, &sentinel, sizeof(output)) == 0);
    fake.regs[LSM6DSO_STATUS_REG] = 1; /* Only accel ready is insufficient. */
    assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_NO_SAMPLE);
    fake.regs[LSM6DSO_STATUS_REG] = 2; /* Only gyro ready is insufficient. */
    assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_NO_SAMPLE);
    fake.regs[LSM6DSO_STATUS_REG] = 3;
    raw_word(LSM6DSO_OUTX_L_G, 1000);
    raw_word(LSM6DSO_OUTX_L_G + 2, -1000);
    raw_word(LSM6DSO_OUTX_L_G + 4, INT16_MIN);
    raw_word(LSM6DSO_OUTX_L_G + 6, 1000);
    raw_word(LSM6DSO_OUTX_L_G + 8, -1000);
    raw_word(LSM6DSO_OUTX_L_G + 10, INT16_MAX);
    assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_OK);
    near(output.acceleration_mps2[0], 1.1964113f);
    near(output.acceleration_mps2[1], -1.1964113f);
    near(output.acceleration_mps2[2], 32767 * 0.122f * 0.00980665f);
    near(output.angular_velocity_radps[0], 0.305432619f);
    near(output.angular_velocity_radps[1], -0.305432619f);
    near(output.angular_velocity_radps[2], -32768 * 17.5f * 0.0000174532925199433f);

    for (unsigned fail = 1; fail <= 2; ++fail) {
        output = sentinel;
        fake.fail_at = fake.transactions + fail;
        assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_IO_ERROR);
        assert(memcmp(&output, &sentinel, sizeof(output)) == 0);
    }
    fake.fail_at = 0;
    assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_OK);
    carelink_lsm6dso_power_down(&device);
    assert(!device.configured);
    assert((fake.regs[LSM6DSO_CTRL1_XL] & 0xf0) == 0);
    assert((fake.regs[LSM6DSO_CTRL2_G] & 0xf0) == 0);
    assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_INVALID_CONFIG);

    /* Exercise every transport failure in the startup sequence. */
    for (unsigned fail = 1; fail <= init_transactions; ++fail) {
        device = new_device();
        fake.fail_at = fail;
        assert(carelink_lsm6dso_init(&device, &config) == CARELINK_LSM6DSO_IO_ERROR);
        assert(!device.configured);
        assert((fake.regs[LSM6DSO_CTRL1_XL] & 0xf0) == 0);
        assert((fake.regs[LSM6DSO_CTRL2_G] & 0xf0) == 0);
        fake.fail_at = 0;
        assert(carelink_lsm6dso_init(&device, &config) == CARELINK_LSM6DSO_OK);
    }
    device = new_device();
    fake.regs[LSM6DSO_WHO_AM_I] = 0;
    assert(carelink_lsm6dso_init(&device, &config) == CARELINK_LSM6DSO_WRONG_ID);
    assert(fake.writes == 0);
    device = new_device();
    fake.stuck_reset = true;
    assert(carelink_lsm6dso_init(&device, &config) == CARELINK_LSM6DSO_RESET_TIMEOUT);
    assert(fake.time_ms == 200);

    const unsigned rates[] = {26, 52, 104, 208};
    const unsigned accel_ranges[] = {2, 4, 8, 16};
    const unsigned gyro_ranges[] = {250, 500, 1000, 2000};
    const unsigned accel_bits[] = {0, 2, 3, 1};
    const unsigned gyro_bits[] = {0, 2, 4, 6};
    const float accel_sensitivity[] = {0.061f, 0.122f, 0.244f, 0.488f};
    const float gyro_sensitivity[] = {8.75f, 17.5f, 35.0f, 70.0f};
    for (unsigned i = 0; i < 4; ++i) {
        device = new_device();
        carelink_lsm6dso_config_t cfg = {rates[i], accel_ranges[i], gyro_ranges[i]};
        assert(carelink_lsm6dso_init(&device, &cfg) == CARELINK_LSM6DSO_OK);
        assert(fake.regs[LSM6DSO_CTRL1_XL] == ((i + 2) << 4 | accel_bits[i] << 2));
        assert(fake.regs[LSM6DSO_CTRL2_G] == ((i + 2) << 4 | gyro_bits[i] << 1));
        fake.regs[LSM6DSO_STATUS_REG] = 3;
        raw_word(LSM6DSO_OUTX_L_G, 1000);
        raw_word(LSM6DSO_OUTX_L_G + 6, 1000);
        assert(carelink_lsm6dso_read(&device, &output) == CARELINK_LSM6DSO_OK);
        near(output.acceleration_mps2[0], 1000 * accel_sensitivity[i] * 0.00980665f);
        near(output.angular_velocity_radps[0], 1000 * gyro_sensitivity[i] * 0.0000174532925199433f);
    }
    device = new_device();
    carelink_lsm6dso_config_t bad[] = {{100, 4, 500}, {104, 3, 500}, {104, 4, 123}};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(carelink_lsm6dso_init(&device, &bad[i]) == CARELINK_LSM6DSO_INVALID_CONFIG);
    assert(fake.transactions == 0);
    assert(carelink_lsm6dso_init(NULL, &config) == CARELINK_LSM6DSO_INVALID_CONFIG);
    assert(carelink_lsm6dso_init(&device, NULL) == CARELINK_LSM6DSO_INVALID_CONFIG);
    return 0;
}
