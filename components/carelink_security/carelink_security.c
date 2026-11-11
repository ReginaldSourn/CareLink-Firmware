#include "carelink_security.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_random.h"
#include "sdkconfig.h"

static const char *const NVS_NAMESPACE = "carelink_sec";
static const char *const KEY_HMAC = "hmac_key";
static const char *const KEY_REPLAY_HIGH = "replay_hi";

static carelink_auth_key_status_t load_hmac_key(void *context,
                                                uint8_t *key,
                                                size_t key_capacity,
                                                size_t *key_size)
{
    carelink_security_t *security = (carelink_security_t *)context;
    if (security == NULL || !security->initialized || key == NULL ||
        key_size == NULL) {
        return CARELINK_AUTH_PROVIDER_KEY_ERROR;
    }
    if (key_capacity < CARELINK_SECURITY_KEY_SIZE) {
        return CARELINK_AUTH_PROVIDER_KEY_ERROR;
    }

    size_t stored_size = 0U;
    esp_err_t error = nvs_get_blob(security->handle, KEY_HMAC, NULL,
                                   &stored_size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return CARELINK_AUTH_PROVIDER_KEY_UNAVAILABLE;
    }
    if (error != ESP_OK || stored_size != CARELINK_SECURITY_KEY_SIZE) {
        return CARELINK_AUTH_PROVIDER_KEY_ERROR;
    }
    size_t capacity = key_capacity;
    error = nvs_get_blob(security->handle, KEY_HMAC, key, &capacity);
    if (error != ESP_OK || capacity != CARELINK_SECURITY_KEY_SIZE) {
        memset(key, 0, key_capacity);
        return CARELINK_AUTH_PROVIDER_KEY_ERROR;
    }
    *key_size = capacity;
    return CARELINK_AUTH_PROVIDER_KEY_OK;
}

esp_err_t carelink_security_init(carelink_security_t *security)
{
    if (security == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(security, 0, sizeof(*security));
    const esp_err_t error =
        nvs_open(NVS_NAMESPACE, NVS_READWRITE, &security->handle);
    if (error == ESP_OK) {
        security->initialized = true;
    }
    return error;
}

carelink_auth_key_provider_t carelink_security_key_provider(
    carelink_security_t *security)
{
    const carelink_auth_key_provider_t provider = {
        .get_key = load_hmac_key,
        .context = security,
    };
    return provider;
}

esp_err_t carelink_security_device_id(uint64_t *device_id)
{
    if (device_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mac[6];
    const esp_err_t error = esp_read_mac(mac, ESP_MAC_BASE);
    if (error != ESP_OK) {
        return error;
    }
    uint64_t value = 0U;
    for (size_t index = 0U; index < sizeof(mac); ++index) {
        value = (value << 8) | mac[index];
    }
    if (value == 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *device_id = value;
    return ESP_OK;
}

uint32_t carelink_security_new_session_id(void)
{
    uint32_t session_id = 0U;
    while (session_id == 0U) {
        session_id = esp_random();
    }
    return session_id;
}

static esp_err_t reserve_counter_window(carelink_security_t *security)
{
    uint64_t high = 0U;
    esp_err_t error = nvs_get_u64(security->handle, KEY_REPLAY_HIGH, &high);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        high = 0U;
    } else if (error != ESP_OK) {
        return error;
    }
    const uint64_t window = (uint64_t)CONFIG_CARELINK_REPLAY_COUNTER_WINDOW;
    if (high > UINT64_MAX - window) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t new_high = high + window;
    error = nvs_set_u64(security->handle, KEY_REPLAY_HIGH, new_high);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_commit(security->handle);
    if (error != ESP_OK) {
        return error;
    }
    security->next_counter = high + 1U;
    security->reserved_through = new_high;
    return ESP_OK;
}

esp_err_t carelink_security_next_replay_counter(carelink_security_t *security,
                                                uint64_t *counter)
{
    if (security == NULL || counter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!security->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (security->next_counter == 0U ||
        security->next_counter > security->reserved_through) {
        const esp_err_t error = reserve_counter_window(security);
        if (error != ESP_OK) {
            return error;
        }
    }
    *counter = security->next_counter++;
    return ESP_OK;
}
