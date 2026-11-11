#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "carelink_config_codec.h"

static carelink_runtime_config_t valid_config(void)
{
    const carelink_runtime_config_t config = {
        .radio_frequency_hz = 915000000U,
        .retry_first_ms = 2000U,
        .retry_max_ms = 60000U,
        .location_update_ms = 60000U,
        .gps_stale_ms = 5000U,
        .radio_tx_power_dbm = -3,
        .maximum_attempts = 12U,
        .oled_enabled = true,
    };
    return config;
}

static void assert_equal(const carelink_runtime_config_t *left,
                         const carelink_runtime_config_t *right)
{
    assert(left->radio_frequency_hz == right->radio_frequency_hz);
    assert(left->retry_first_ms == right->retry_first_ms);
    assert(left->retry_max_ms == right->retry_max_ms);
    assert(left->location_update_ms == right->location_update_ms);
    assert(left->gps_stale_ms == right->gps_stale_ms);
    assert(left->radio_tx_power_dbm == right->radio_tx_power_dbm);
    assert(left->maximum_attempts == right->maximum_attempts);
    assert(left->oled_enabled == right->oled_enabled);
}

static void test_wire_round_trip(void)
{
    const carelink_runtime_config_t input = valid_config();
    uint8_t wire[CARELINK_CONFIG_WIRE_SIZE];
    assert(carelink_config_wire_encode(&input, wire) ==
           CARELINK_CONFIG_CODEC_OK);
    assert(wire[0] == 1U && wire[1] == 1U);
    assert(wire[2] == (uint8_t)-3 && wire[3] == 12U);
    assert(wire[4] == 0xc0U && wire[5] == 0xcaU &&
           wire[6] == 0x89U && wire[7] == 0x36U);
    for (size_t index = 24U; index < sizeof(wire); ++index) {
        assert(wire[index] == 0U);
    }
    carelink_runtime_config_t output = {0};
    assert(carelink_config_wire_decode(wire, sizeof(wire), &output) ==
           CARELINK_CONFIG_CODEC_OK);
    assert_equal(&input, &output);
}

static void test_validation(void)
{
    carelink_runtime_config_t config = valid_config();
    assert(carelink_runtime_config_valid(&config));
    config.radio_frequency_hz = 800000000U;
    assert(!carelink_runtime_config_valid(&config));
    config = valid_config();
    config.radio_tx_power_dbm = 15;
    assert(!carelink_runtime_config_valid(&config));
    config = valid_config();
    config.retry_max_ms = config.retry_first_ms - 1U;
    assert(!carelink_runtime_config_valid(&config));
    config = valid_config();
    config.maximum_attempts = 0U;
    assert(!carelink_runtime_config_valid(&config));
    config = valid_config();
    config.location_update_ms = 9999U;
    assert(!carelink_runtime_config_valid(&config));
    config = valid_config();
    config.gps_stale_ms = 60001U;
    assert(!carelink_runtime_config_valid(&config));
}

static void test_wire_rejection(void)
{
    const carelink_runtime_config_t config = valid_config();
    uint8_t wire[CARELINK_CONFIG_WIRE_SIZE];
    carelink_runtime_config_t output;
    assert(carelink_config_wire_encode(&config, wire) ==
           CARELINK_CONFIG_CODEC_OK);
    assert(carelink_config_wire_decode(wire, sizeof(wire) - 1U, &output) ==
           CARELINK_CONFIG_CODEC_INVALID_SIZE);
    wire[0] = 2U;
    assert(carelink_config_wire_decode(wire, sizeof(wire), &output) ==
           CARELINK_CONFIG_CODEC_UNSUPPORTED_VERSION);
    wire[0] = 1U;
    wire[1] = 0x80U;
    assert(carelink_config_wire_decode(wire, sizeof(wire), &output) ==
           CARELINK_CONFIG_CODEC_INVALID_FIELD);
    wire[1] = 1U;
    wire[27] = 1U;
    assert(carelink_config_wire_decode(wire, sizeof(wire), &output) ==
           CARELINK_CONFIG_CODEC_INVALID_FIELD);
}

static void test_record_integrity(void)
{
    const carelink_runtime_config_t input = valid_config();
    uint8_t record[CARELINK_CONFIG_RECORD_SIZE];
    assert(carelink_config_record_encode(&input, UINT32_MAX, record) ==
           CARELINK_CONFIG_CODEC_OK);
    assert(memcmp(record, "CLC1", 4U) == 0);
    carelink_runtime_config_t output;
    uint32_t generation = 0U;
    assert(carelink_config_record_decode(record, sizeof(record), &output,
                                         &generation) ==
           CARELINK_CONFIG_CODEC_OK);
    assert(generation == UINT32_MAX);
    assert_equal(&input, &output);

    for (size_t index = 0U; index < 40U; ++index) {
        uint8_t corrupted[CARELINK_CONFIG_RECORD_SIZE];
        memcpy(corrupted, record, sizeof(corrupted));
        corrupted[index] ^= 0x40U;
        assert(carelink_config_record_decode(corrupted, sizeof(corrupted),
                                             &output, &generation) !=
               CARELINK_CONFIG_CODEC_OK);
    }
    assert(carelink_config_generation_newer(0U, UINT32_MAX));
    assert(!carelink_config_generation_newer(UINT32_MAX, 0U));
    assert(!carelink_config_generation_newer(7U, 7U));
}

int main(void)
{
    test_wire_round_trip();
    test_validation();
    test_wire_rejection();
    test_record_integrity();
    return 0;
}
