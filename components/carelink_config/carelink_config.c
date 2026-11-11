#include "carelink_config.h"

#include <string.h>

#include "nvs.h"
#include "sdkconfig.h"

static const char *const NVS_NAMESPACE = "carelink_cfg";
static const char *const SLOT_KEYS[2] = {"slot_a", "slot_b"};

#ifdef CONFIG_CARELINK_RADIO_FREQUENCY_HZ
#define CARELINK_DEFAULT_FREQUENCY_HZ CONFIG_CARELINK_RADIO_FREQUENCY_HZ
#else
#define CARELINK_DEFAULT_FREQUENCY_HZ 915000000
#endif
#ifdef CONFIG_CARELINK_RADIO_TX_POWER_DBM
#define CARELINK_DEFAULT_TX_POWER_DBM CONFIG_CARELINK_RADIO_TX_POWER_DBM
#else
#define CARELINK_DEFAULT_TX_POWER_DBM 0
#endif
#ifdef CONFIG_CARELINK_INCIDENT_RETRY_FIRST_MS
#define CARELINK_DEFAULT_RETRY_FIRST_MS CONFIG_CARELINK_INCIDENT_RETRY_FIRST_MS
#else
#define CARELINK_DEFAULT_RETRY_FIRST_MS 2000
#endif
#ifdef CONFIG_CARELINK_INCIDENT_RETRY_MAX_MS
#define CARELINK_DEFAULT_RETRY_MAX_MS CONFIG_CARELINK_INCIDENT_RETRY_MAX_MS
#else
#define CARELINK_DEFAULT_RETRY_MAX_MS 60000
#endif
#ifdef CONFIG_CARELINK_INCIDENT_LOCATION_UPDATE_MS
#define CARELINK_DEFAULT_LOCATION_UPDATE_MS \
    CONFIG_CARELINK_INCIDENT_LOCATION_UPDATE_MS
#else
#define CARELINK_DEFAULT_LOCATION_UPDATE_MS 60000
#endif
#ifdef CONFIG_CARELINK_INCIDENT_MAX_ATTEMPTS
#define CARELINK_DEFAULT_MAX_ATTEMPTS CONFIG_CARELINK_INCIDENT_MAX_ATTEMPTS
#else
#define CARELINK_DEFAULT_MAX_ATTEMPTS 12
#endif

typedef struct {
    uint8_t data[CARELINK_CONFIG_RECORD_SIZE];
    size_t size;
    bool present;
    bool valid;
    uint32_t generation;
    carelink_runtime_config_t config;
} config_slot_t;

void carelink_config_defaults(carelink_runtime_config_t *config)
{
    if (config == NULL) {
        return;
    }
    *config = (carelink_runtime_config_t){
        .radio_frequency_hz = CARELINK_DEFAULT_FREQUENCY_HZ,
        .retry_first_ms = CARELINK_DEFAULT_RETRY_FIRST_MS,
        .retry_max_ms = CARELINK_DEFAULT_RETRY_MAX_MS,
        .location_update_ms = CARELINK_DEFAULT_LOCATION_UPDATE_MS,
        .gps_stale_ms = CONFIG_CARELINK_GPS_STALE_MS,
        .radio_tx_power_dbm = CARELINK_DEFAULT_TX_POWER_DBM,
        .maximum_attempts = CARELINK_DEFAULT_MAX_ATTEMPTS,
#if CONFIG_CARELINK_OLED_ENABLED
        .oled_enabled = true,
#else
        .oled_enabled = false,
#endif
    };
}

static esp_err_t read_slot(nvs_handle_t handle,
                           const char *key,
                           config_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
    size_t stored_size = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &stored_size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    slot->present = true;
    slot->size = stored_size;
    if (stored_size != sizeof(slot->data)) {
        return ESP_OK;
    }
    size_t capacity = sizeof(slot->data);
    error = nvs_get_blob(handle, key, slot->data, &capacity);
    if (error != ESP_OK) {
        return error;
    }
    slot->size = capacity;
    slot->valid = carelink_config_record_decode(
                      slot->data, slot->size, &slot->config,
                      &slot->generation) == CARELINK_CONFIG_CODEC_OK;
    return ESP_OK;
}

static int select_slot(const config_slot_t slots[2])
{
    if (!slots[0].valid && !slots[1].valid) {
        return -1;
    }
    if (slots[1].valid &&
        (!slots[0].valid || carelink_config_generation_newer(
                                slots[1].generation,
                                slots[0].generation))) {
        return 1;
    }
    return 0;
}

static esp_err_t load_slots(nvs_handle_t handle, config_slot_t slots[2])
{
    esp_err_t error = read_slot(handle, SLOT_KEYS[0], &slots[0]);
    if (error == ESP_OK) {
        error = read_slot(handle, SLOT_KEYS[1], &slots[1]);
    }
    return error;
}

esp_err_t carelink_config_load(carelink_runtime_config_t *config,
                               carelink_config_source_t *source)
{
    if (config == NULL || source == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    carelink_config_defaults(config);
    if (!carelink_runtime_config_valid(config)) {
        return ESP_ERR_INVALID_STATE;
    }
    *source = CARELINK_CONFIG_SOURCE_DEFAULTS;

    nvs_handle_t handle;
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    config_slot_t slots[2];
    error = load_slots(handle, slots);
    nvs_close(handle);
    if (error != ESP_OK) {
        return error;
    }
    const int selected = select_slot(slots);
    if (selected >= 0) {
        *config = slots[selected].config;
        *source = CARELINK_CONFIG_SOURCE_RUNTIME;
    } else if (slots[0].present || slots[1].present) {
        *source = CARELINK_CONFIG_SOURCE_RECOVERY;
    }
    return ESP_OK;
}

esp_err_t carelink_config_save(const carelink_runtime_config_t *config)
{
    if (!carelink_runtime_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    config_slot_t slots[2];
    error = load_slots(handle, slots);
    if (error != ESP_OK) {
        nvs_close(handle);
        return error;
    }
    const int active = select_slot(slots);
    const unsigned target = active == 0 ? 1U : 0U;
    const uint32_t generation =
        active >= 0 ? slots[active].generation + 1U : 1U;
    uint8_t record[CARELINK_CONFIG_RECORD_SIZE];
    if (carelink_config_record_encode(config, generation, record) !=
        CARELINK_CONFIG_CODEC_OK) {
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }
    error = nvs_set_blob(handle, SLOT_KEYS[target], record, sizeof(record));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    memset(record, 0, sizeof(record));
    return error;
}
