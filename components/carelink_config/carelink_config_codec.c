#include "carelink_config_codec.h"

#include <string.h>

enum {
    WIRE_OFFSET_VERSION = 0,
    WIRE_OFFSET_FLAGS = 1,
    WIRE_OFFSET_TX_POWER = 2,
    WIRE_OFFSET_ATTEMPTS = 3,
    WIRE_OFFSET_FREQUENCY = 4,
    WIRE_OFFSET_RETRY_FIRST = 8,
    WIRE_OFFSET_RETRY_MAX = 12,
    WIRE_OFFSET_LOCATION_UPDATE = 16,
    WIRE_OFFSET_GPS_STALE = 20,
    WIRE_OFFSET_RESERVED = 24,
    RECORD_OFFSET_MAGIC = 0,
    RECORD_OFFSET_SCHEMA = 4,
    RECORD_OFFSET_LENGTH = 6,
    RECORD_OFFSET_GENERATION = 8,
    RECORD_OFFSET_CONFIG = 12,
    RECORD_OFFSET_CRC32 = 40,
};

static const uint8_t RECORD_MAGIC[4] = {'C', 'L', 'C', '1'};
static const uint8_t FLAG_OLED_ENABLED = UINT8_C(1) << 0;

_Static_assert(WIRE_OFFSET_RESERVED + 4U == CARELINK_CONFIG_WIRE_SIZE,
               "runtime configuration wire layout must be exact");
_Static_assert(RECORD_OFFSET_CRC32 + 4U == CARELINK_CONFIG_RECORD_SIZE,
               "runtime configuration record layout must be exact");

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index) {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *source)
{
    uint32_t value = 0U;
    for (size_t index = 0U; index < 4U; ++index) {
        value |= (uint32_t)source[index] << (index * 8U);
    }
    return value;
}

static uint32_t crc32_ieee(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static bool valid_frequency(uint32_t frequency_hz)
{
    return (frequency_hz >= 430000000U && frequency_hz <= 440000000U) ||
           (frequency_hz >= 470000000U && frequency_hz <= 510000000U) ||
           (frequency_hz >= 779000000U && frequency_hz <= 787000000U) ||
           (frequency_hz >= 863000000U && frequency_hz <= 870000000U) ||
           (frequency_hz >= 902000000U && frequency_hz <= 928000000U);
}

bool carelink_runtime_config_valid(const carelink_runtime_config_t *config)
{
    return config != NULL && valid_frequency(config->radio_frequency_hz) &&
           config->radio_tx_power_dbm >= -9 &&
           config->radio_tx_power_dbm <= 14 &&
           config->retry_first_ms >= 500U &&
           config->retry_first_ms <= 60000U &&
           config->retry_max_ms >= config->retry_first_ms &&
           config->retry_max_ms <= 3600000U &&
           config->maximum_attempts >= 1U &&
           config->maximum_attempts <= 100U &&
           config->location_update_ms >= 10000U &&
           config->location_update_ms <= 3600000U &&
           config->gps_stale_ms >= 1000U &&
           config->gps_stale_ms <= 60000U;
}

carelink_config_codec_result_t carelink_config_wire_encode(
    const carelink_runtime_config_t *config,
    uint8_t output[CARELINK_CONFIG_WIRE_SIZE])
{
    if (config == NULL || output == NULL) {
        return CARELINK_CONFIG_CODEC_INVALID_ARGUMENT;
    }
    if (!carelink_runtime_config_valid(config)) {
        return CARELINK_CONFIG_CODEC_INVALID_FIELD;
    }
    memset(output, 0, CARELINK_CONFIG_WIRE_SIZE);
    output[WIRE_OFFSET_VERSION] = CARELINK_CONFIG_WIRE_VERSION;
    output[WIRE_OFFSET_FLAGS] =
        config->oled_enabled ? FLAG_OLED_ENABLED : 0U;
    output[WIRE_OFFSET_TX_POWER] = (uint8_t)config->radio_tx_power_dbm;
    output[WIRE_OFFSET_ATTEMPTS] = config->maximum_attempts;
    put_u32_le(&output[WIRE_OFFSET_FREQUENCY], config->radio_frequency_hz);
    put_u32_le(&output[WIRE_OFFSET_RETRY_FIRST], config->retry_first_ms);
    put_u32_le(&output[WIRE_OFFSET_RETRY_MAX], config->retry_max_ms);
    put_u32_le(&output[WIRE_OFFSET_LOCATION_UPDATE],
               config->location_update_ms);
    put_u32_le(&output[WIRE_OFFSET_GPS_STALE], config->gps_stale_ms);
    return CARELINK_CONFIG_CODEC_OK;
}

carelink_config_codec_result_t carelink_config_wire_decode(
    const uint8_t *data,
    size_t size,
    carelink_runtime_config_t *config)
{
    if (data == NULL || config == NULL) {
        return CARELINK_CONFIG_CODEC_INVALID_ARGUMENT;
    }
    if (size != CARELINK_CONFIG_WIRE_SIZE) {
        return CARELINK_CONFIG_CODEC_INVALID_SIZE;
    }
    if (data[WIRE_OFFSET_VERSION] != CARELINK_CONFIG_WIRE_VERSION) {
        return CARELINK_CONFIG_CODEC_UNSUPPORTED_VERSION;
    }
    if ((data[WIRE_OFFSET_FLAGS] & (uint8_t)~FLAG_OLED_ENABLED) != 0U ||
        get_u32_le(&data[WIRE_OFFSET_RESERVED]) != 0U) {
        return CARELINK_CONFIG_CODEC_INVALID_FIELD;
    }
    const carelink_runtime_config_t decoded = {
        .radio_frequency_hz = get_u32_le(&data[WIRE_OFFSET_FREQUENCY]),
        .retry_first_ms = get_u32_le(&data[WIRE_OFFSET_RETRY_FIRST]),
        .retry_max_ms = get_u32_le(&data[WIRE_OFFSET_RETRY_MAX]),
        .location_update_ms = get_u32_le(&data[WIRE_OFFSET_LOCATION_UPDATE]),
        .gps_stale_ms = get_u32_le(&data[WIRE_OFFSET_GPS_STALE]),
        .radio_tx_power_dbm = (int8_t)data[WIRE_OFFSET_TX_POWER],
        .maximum_attempts = data[WIRE_OFFSET_ATTEMPTS],
        .oled_enabled =
            (data[WIRE_OFFSET_FLAGS] & FLAG_OLED_ENABLED) != 0U,
    };
    if (!carelink_runtime_config_valid(&decoded)) {
        return CARELINK_CONFIG_CODEC_INVALID_FIELD;
    }
    *config = decoded;
    return CARELINK_CONFIG_CODEC_OK;
}

carelink_config_codec_result_t carelink_config_record_encode(
    const carelink_runtime_config_t *config,
    uint32_t generation,
    uint8_t output[CARELINK_CONFIG_RECORD_SIZE])
{
    if (config == NULL || output == NULL) {
        return CARELINK_CONFIG_CODEC_INVALID_ARGUMENT;
    }
    memset(output, 0, CARELINK_CONFIG_RECORD_SIZE);
    memcpy(&output[RECORD_OFFSET_MAGIC], RECORD_MAGIC, sizeof(RECORD_MAGIC));
    put_u16_le(&output[RECORD_OFFSET_SCHEMA], CARELINK_CONFIG_WIRE_VERSION);
    put_u16_le(&output[RECORD_OFFSET_LENGTH], CARELINK_CONFIG_RECORD_SIZE);
    put_u32_le(&output[RECORD_OFFSET_GENERATION], generation);
    const carelink_config_codec_result_t result = carelink_config_wire_encode(
        config, &output[RECORD_OFFSET_CONFIG]);
    if (result != CARELINK_CONFIG_CODEC_OK) {
        return result;
    }
    put_u32_le(&output[RECORD_OFFSET_CRC32],
               crc32_ieee(output, RECORD_OFFSET_CRC32));
    return CARELINK_CONFIG_CODEC_OK;
}

carelink_config_codec_result_t carelink_config_record_decode(
    const uint8_t *data,
    size_t size,
    carelink_runtime_config_t *config,
    uint32_t *generation)
{
    if (data == NULL || config == NULL || generation == NULL) {
        return CARELINK_CONFIG_CODEC_INVALID_ARGUMENT;
    }
    if (size != CARELINK_CONFIG_RECORD_SIZE ||
        get_u16_le(&data[RECORD_OFFSET_LENGTH]) !=
            CARELINK_CONFIG_RECORD_SIZE) {
        return CARELINK_CONFIG_CODEC_INVALID_SIZE;
    }
    if (memcmp(&data[RECORD_OFFSET_MAGIC], RECORD_MAGIC,
               sizeof(RECORD_MAGIC)) != 0) {
        return CARELINK_CONFIG_CODEC_INVALID_MAGIC;
    }
    if (get_u16_le(&data[RECORD_OFFSET_SCHEMA]) !=
        CARELINK_CONFIG_WIRE_VERSION) {
        return CARELINK_CONFIG_CODEC_UNSUPPORTED_VERSION;
    }
    if (get_u32_le(&data[RECORD_OFFSET_CRC32]) !=
        crc32_ieee(data, RECORD_OFFSET_CRC32)) {
        return CARELINK_CONFIG_CODEC_INVALID_CRC;
    }
    const carelink_config_codec_result_t result = carelink_config_wire_decode(
        &data[RECORD_OFFSET_CONFIG], CARELINK_CONFIG_WIRE_SIZE, config);
    if (result == CARELINK_CONFIG_CODEC_OK) {
        *generation = get_u32_le(&data[RECORD_OFFSET_GENERATION]);
    }
    return result;
}

bool carelink_config_generation_newer(uint32_t candidate,
                                      uint32_t reference)
{
    const uint32_t difference = candidate - reference;
    return difference != 0U && difference < UINT32_C(0x80000000);
}
