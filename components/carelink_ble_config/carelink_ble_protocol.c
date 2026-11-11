#include "carelink_ble_protocol.h"

#include <string.h>

enum {
    MAX_CONFIG_SOURCE = 2,
    MAX_INCIDENT_STATE = 7,
    KNOWN_CAPABILITIES = CARELINK_BLE_CAPABILITY_OLED |
                         CARELINK_BLE_CAPABILITY_RAW_LORA |
                         CARELINK_BLE_CAPABILITY_INCIDENT_POLICY |
                         CARELINK_BLE_CAPABILITY_GNSS_FRESHNESS |
                         CARELINK_BLE_CAPABILITY_RESTORE_DEFAULTS,
};

static void put_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

carelink_ble_protocol_result_t carelink_ble_status_encode(
    const carelink_ble_status_t *status,
    uint8_t output[CARELINK_BLE_STATUS_WIRE_SIZE])
{
    if (status == NULL || output == NULL) {
        return CARELINK_BLE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (status->config_wire_version == 0U ||
        status->config_source > MAX_CONFIG_SOURCE ||
        status->incident_state > MAX_INCIDENT_STATE ||
        (status->capabilities & ~((uint32_t)KNOWN_CAPABILITIES)) != 0U) {
        return CARELINK_BLE_PROTOCOL_INVALID_FIELD;
    }

    memset(output, 0, CARELINK_BLE_STATUS_WIRE_SIZE);
    output[0] = CARELINK_BLE_STATUS_VERSION;
    if (status->configuration_locked) {
        output[1] |= CARELINK_BLE_STATUS_FLAG_CONFIGURATION_LOCKED;
    }
    if (status->restart_pending) {
        output[1] |= CARELINK_BLE_STATUS_FLAG_RESTART_PENDING;
    }
    if (status->apply_deferred) {
        output[1] |= CARELINK_BLE_STATUS_FLAG_APPLY_DEFERRED;
    }
    if (status->incident_running) {
        output[1] |= CARELINK_BLE_STATUS_FLAG_INCIDENT_RUNNING;
    }
    output[2] = status->config_wire_version;
    output[3] = status->config_source;
    output[4] = status->incident_state;
    put_u32_le(&output[8], status->capabilities);
    return CARELINK_BLE_PROTOCOL_OK;
}

carelink_ble_protocol_result_t carelink_ble_control_encode(
    carelink_ble_control_command_t command,
    uint8_t output[CARELINK_BLE_CONTROL_WIRE_SIZE])
{
    if (output == NULL) {
        return CARELINK_BLE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (command != CARELINK_BLE_CONTROL_NONE &&
        command != CARELINK_BLE_CONTROL_RESTORE_DEFAULTS) {
        return CARELINK_BLE_PROTOCOL_INVALID_FIELD;
    }
    output[0] = CARELINK_BLE_CONTROL_VERSION;
    output[1] = (uint8_t)command;
    output[2] = 0U;
    output[3] = 0U;
    return CARELINK_BLE_PROTOCOL_OK;
}

carelink_ble_protocol_result_t carelink_ble_control_decode(
    const uint8_t *input,
    size_t input_size,
    carelink_ble_control_command_t *command)
{
    if (input == NULL || command == NULL) {
        return CARELINK_BLE_PROTOCOL_INVALID_ARGUMENT;
    }
    if (input_size != CARELINK_BLE_CONTROL_WIRE_SIZE) {
        return CARELINK_BLE_PROTOCOL_INVALID_SIZE;
    }
    if (input[0] != CARELINK_BLE_CONTROL_VERSION) {
        return CARELINK_BLE_PROTOCOL_UNSUPPORTED_VERSION;
    }
    if (input[1] > (uint8_t)CARELINK_BLE_CONTROL_RESTORE_DEFAULTS ||
        input[2] != 0U || input[3] != 0U) {
        return CARELINK_BLE_PROTOCOL_INVALID_FIELD;
    }
    *command = (carelink_ble_control_command_t)input[1];
    return CARELINK_BLE_PROTOCOL_OK;
}
