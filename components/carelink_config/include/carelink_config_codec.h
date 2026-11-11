#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_CONFIG_WIRE_VERSION 1U
#define CARELINK_CONFIG_WIRE_SIZE 28U
#define CARELINK_CONFIG_RECORD_SIZE 44U

typedef struct {
    uint32_t radio_frequency_hz;
    uint32_t retry_first_ms;
    uint32_t retry_max_ms;
    uint32_t location_update_ms;
    uint32_t gps_stale_ms;
    int8_t radio_tx_power_dbm;
    uint8_t maximum_attempts;
    bool oled_enabled;
} carelink_runtime_config_t;

typedef enum {
    CARELINK_CONFIG_CODEC_OK = 0,
    CARELINK_CONFIG_CODEC_INVALID_ARGUMENT,
    CARELINK_CONFIG_CODEC_INVALID_SIZE,
    CARELINK_CONFIG_CODEC_UNSUPPORTED_VERSION,
    CARELINK_CONFIG_CODEC_INVALID_FIELD,
    CARELINK_CONFIG_CODEC_INVALID_MAGIC,
    CARELINK_CONFIG_CODEC_INVALID_CRC,
} carelink_config_codec_result_t;

bool carelink_runtime_config_valid(const carelink_runtime_config_t *config);

carelink_config_codec_result_t carelink_config_wire_encode(
    const carelink_runtime_config_t *config,
    uint8_t output[CARELINK_CONFIG_WIRE_SIZE]);

carelink_config_codec_result_t carelink_config_wire_decode(
    const uint8_t *data,
    size_t size,
    carelink_runtime_config_t *config);

carelink_config_codec_result_t carelink_config_record_encode(
    const carelink_runtime_config_t *config,
    uint32_t generation,
    uint8_t output[CARELINK_CONFIG_RECORD_SIZE]);

carelink_config_codec_result_t carelink_config_record_decode(
    const uint8_t *data,
    size_t size,
    carelink_runtime_config_t *config,
    uint32_t *generation);

bool carelink_config_generation_newer(uint32_t candidate,
                                      uint32_t reference);

#ifdef __cplusplus
}
#endif
