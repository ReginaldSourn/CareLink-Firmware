#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "carelink_ble_protocol.h"
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

static void test_ble_status_wire(void)
{
    const carelink_ble_status_t status = {
        .config_wire_version = CARELINK_CONFIG_WIRE_VERSION,
        .config_source = 2U,
        .incident_state = 3U,
        .capabilities = CARELINK_BLE_CAPABILITY_OLED |
                        CARELINK_BLE_CAPABILITY_RAW_LORA |
                        CARELINK_BLE_CAPABILITY_RESTORE_DEFAULTS,
        .configuration_locked = true,
        .restart_pending = false,
        .apply_deferred = true,
        .incident_running = true,
    };
    uint8_t wire[CARELINK_BLE_STATUS_WIRE_SIZE];
    assert(carelink_ble_status_encode(&status, wire) ==
           CARELINK_BLE_PROTOCOL_OK);
    assert(wire[0] == CARELINK_BLE_STATUS_VERSION);
    assert(wire[1] == (CARELINK_BLE_STATUS_FLAG_CONFIGURATION_LOCKED |
                       CARELINK_BLE_STATUS_FLAG_APPLY_DEFERRED |
                       CARELINK_BLE_STATUS_FLAG_INCIDENT_RUNNING));
    assert(wire[2] == CARELINK_CONFIG_WIRE_VERSION);
    assert(wire[3] == 2U);
    assert(wire[4] == 3U);
    assert(wire[5] == 0U && wire[6] == 0U && wire[7] == 0U);
    assert(wire[8] == 0x13U);
    assert(wire[9] == 0U && wire[10] == 0U && wire[11] == 0U);

    carelink_ble_status_t invalid = status;
    invalid.config_source = 3U;
    assert(carelink_ble_status_encode(&invalid, wire) ==
           CARELINK_BLE_PROTOCOL_INVALID_FIELD);
    invalid = status;
    invalid.incident_state = 8U;
    assert(carelink_ble_status_encode(&invalid, wire) ==
           CARELINK_BLE_PROTOCOL_INVALID_FIELD);
    invalid = status;
    invalid.capabilities = UINT32_C(1) << 31;
    assert(carelink_ble_status_encode(&invalid, wire) ==
           CARELINK_BLE_PROTOCOL_INVALID_FIELD);
    assert(carelink_ble_status_encode(NULL, wire) ==
           CARELINK_BLE_PROTOCOL_INVALID_ARGUMENT);
}

static void test_ble_control_wire(void)
{
    uint8_t wire[CARELINK_BLE_CONTROL_WIRE_SIZE];
    carelink_ble_control_command_t command = CARELINK_BLE_CONTROL_NONE;
    assert(carelink_ble_control_encode(CARELINK_BLE_CONTROL_RESTORE_DEFAULTS,
                                       wire) == CARELINK_BLE_PROTOCOL_OK);
    assert(wire[0] == CARELINK_BLE_CONTROL_VERSION && wire[1] == 1U &&
           wire[2] == 0U && wire[3] == 0U);
    assert(carelink_ble_control_decode(wire, sizeof(wire), &command) ==
           CARELINK_BLE_PROTOCOL_OK);
    assert(command == CARELINK_BLE_CONTROL_RESTORE_DEFAULTS);

    assert(carelink_ble_control_decode(wire, sizeof(wire) - 1U, &command) ==
           CARELINK_BLE_PROTOCOL_INVALID_SIZE);
    wire[0] = 2U;
    assert(carelink_ble_control_decode(wire, sizeof(wire), &command) ==
           CARELINK_BLE_PROTOCOL_UNSUPPORTED_VERSION);
    wire[0] = CARELINK_BLE_CONTROL_VERSION;
    wire[1] = 2U;
    assert(carelink_ble_control_decode(wire, sizeof(wire), &command) ==
           CARELINK_BLE_PROTOCOL_INVALID_FIELD);
    wire[1] = 1U;
    wire[3] = 1U;
    assert(carelink_ble_control_decode(wire, sizeof(wire), &command) ==
           CARELINK_BLE_PROTOCOL_INVALID_FIELD);
    assert(carelink_ble_control_encode((carelink_ble_control_command_t)2,
                                       wire) ==
           CARELINK_BLE_PROTOCOL_INVALID_FIELD);
}

int main(void)
{
    test_wire_round_trip();
    test_validation();
    test_wire_rejection();
    test_record_integrity();
    test_ble_status_wire();
    test_ble_control_wire();
    return 0;
}
