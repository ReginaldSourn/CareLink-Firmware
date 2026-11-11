#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_BLE_STATUS_WIRE_SIZE 12U
#define CARELINK_BLE_CONTROL_WIRE_SIZE 4U

#define CARELINK_BLE_STATUS_VERSION 1U
#define CARELINK_BLE_CONTROL_VERSION 1U

#define CARELINK_BLE_STATUS_FLAG_CONFIGURATION_LOCKED (UINT8_C(1) << 0)
#define CARELINK_BLE_STATUS_FLAG_RESTART_PENDING (UINT8_C(1) << 1)
#define CARELINK_BLE_STATUS_FLAG_APPLY_DEFERRED (UINT8_C(1) << 2)
#define CARELINK_BLE_STATUS_FLAG_INCIDENT_RUNNING (UINT8_C(1) << 3)

#define CARELINK_BLE_CAPABILITY_OLED (UINT32_C(1) << 0)
#define CARELINK_BLE_CAPABILITY_RAW_LORA (UINT32_C(1) << 1)
#define CARELINK_BLE_CAPABILITY_INCIDENT_POLICY (UINT32_C(1) << 2)
#define CARELINK_BLE_CAPABILITY_GNSS_FRESHNESS (UINT32_C(1) << 3)
#define CARELINK_BLE_CAPABILITY_RESTORE_DEFAULTS (UINT32_C(1) << 4)

typedef enum {
    CARELINK_BLE_PROTOCOL_OK = 0,
    CARELINK_BLE_PROTOCOL_INVALID_ARGUMENT,
    CARELINK_BLE_PROTOCOL_INVALID_SIZE,
    CARELINK_BLE_PROTOCOL_UNSUPPORTED_VERSION,
    CARELINK_BLE_PROTOCOL_INVALID_FIELD,
} carelink_ble_protocol_result_t;

typedef enum {
    CARELINK_BLE_CONTROL_NONE = 0,
    CARELINK_BLE_CONTROL_RESTORE_DEFAULTS = 1,
} carelink_ble_control_command_t;

typedef struct {
    uint8_t config_wire_version;
    uint8_t config_source;
    uint8_t incident_state;
    uint32_t capabilities;
    bool configuration_locked;
    bool restart_pending;
    bool apply_deferred;
    bool incident_running;
} carelink_ble_status_t;

carelink_ble_protocol_result_t carelink_ble_status_encode(
    const carelink_ble_status_t *status,
    uint8_t output[CARELINK_BLE_STATUS_WIRE_SIZE]);

carelink_ble_protocol_result_t carelink_ble_control_encode(
    carelink_ble_control_command_t command,
    uint8_t output[CARELINK_BLE_CONTROL_WIRE_SIZE]);

carelink_ble_protocol_result_t carelink_ble_control_decode(
    const uint8_t *input,
    size_t input_size,
    carelink_ble_control_command_t *command);

#ifdef __cplusplus
}
#endif
