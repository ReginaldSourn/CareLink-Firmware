#include "carelink_security.h"
#include "esp_mac.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool key_present;
static uint8_t stored_key[CARELINK_SECURITY_KEY_SIZE];
static size_t stored_key_size;
static bool high_present;
static uint64_t committed_high;
static uint64_t pending_high;
static bool pending_high_valid;
static bool commit_fails;
static unsigned commits;
static uint8_t base_mac[6];
static esp_err_t mac_error;
static uint32_t random_values[4];
static size_t random_index;

static void reset_mock(void)
{
    key_present = false;
    memset(stored_key, 0, sizeof(stored_key));
    stored_key_size = sizeof(stored_key);
    high_present = false;
    committed_high = pending_high = 0U;
    pending_high_valid = false;
    commit_fails = false;
    commits = 0U;
    const uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(base_mac, mac, sizeof(base_mac));
    mac_error = ESP_OK;
    memset(random_values, 0, sizeof(random_values));
    random_index = 0U;
}

esp_err_t nvs_open(const char *name, int open_mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "carelink_sec") == 0);
    assert(open_mode == NVS_READWRITE && handle != NULL);
    *handle = 7U;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value,
                       size_t *length)
{
    assert(handle == 7U && strcmp(key, "hmac_key") == 0 && length != NULL);
    if (!key_present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (value == NULL) {
        *length = stored_key_size;
        return ESP_OK;
    }
    if (*length < stored_key_size) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(value, stored_key, stored_key_size);
    *length = stored_key_size;
    return ESP_OK;
}

esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *value)
{
    assert(handle == 7U && strcmp(key, "replay_hi") == 0 && value != NULL);
    if (pending_high_valid) {
        *value = pending_high;
        return ESP_OK;
    }
    if (!high_present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = committed_high;
    return ESP_OK;
}

esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value)
{
    assert(handle == 7U && strcmp(key, "replay_hi") == 0);
    pending_high = value;
    pending_high_valid = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 7U);
    ++commits;
    if (commit_fails) {
        return ESP_FAIL;
    }
    if (pending_high_valid) {
        committed_high = pending_high;
        high_present = true;
        pending_high_valid = false;
    }
    return ESP_OK;
}

esp_err_t esp_read_mac(uint8_t *mac, int type)
{
    assert(type == ESP_MAC_BASE && mac != NULL);
    if (mac_error == ESP_OK) {
        memcpy(mac, base_mac, sizeof(base_mac));
    }
    return mac_error;
}

uint32_t esp_random(void)
{
    assert(random_index < sizeof(random_values) / sizeof(random_values[0]));
    return random_values[random_index++];
}

static void test_key_provider_fails_closed(void)
{
    reset_mock();
    carelink_security_t security;
    assert(carelink_security_init(&security) == ESP_OK);
    const carelink_auth_key_provider_t provider =
        carelink_security_key_provider(&security);
    uint8_t key[CARELINK_AUTH_MAX_KEY_SIZE];
    size_t key_size = 99U;
    assert(provider.get_key(provider.context, key, sizeof(key), &key_size) ==
           CARELINK_AUTH_PROVIDER_KEY_UNAVAILABLE);

    key_present = true;
    stored_key_size = CARELINK_SECURITY_KEY_SIZE - 1U;
    assert(provider.get_key(provider.context, key, sizeof(key), &key_size) ==
           CARELINK_AUTH_PROVIDER_KEY_ERROR);
    stored_key_size = CARELINK_SECURITY_KEY_SIZE;
    for (size_t index = 0U; index < sizeof(stored_key); ++index) {
        stored_key[index] = (uint8_t)(index + 1U);
    }
    assert(provider.get_key(provider.context, key, sizeof(key), &key_size) ==
           CARELINK_AUTH_PROVIDER_KEY_OK);
    assert(key_size == CARELINK_SECURITY_KEY_SIZE);
    assert(memcmp(key, stored_key, key_size) == 0);
}

static void test_identity_and_session(void)
{
    reset_mock();
    uint64_t device_id = 0U;
    assert(carelink_security_device_id(&device_id) == ESP_OK);
    assert(device_id == UINT64_C(0x021122334455));
    memset(base_mac, 0, sizeof(base_mac));
    assert(carelink_security_device_id(&device_id) ==
           ESP_ERR_INVALID_RESPONSE);
    mac_error = ESP_FAIL;
    assert(carelink_security_device_id(&device_id) == ESP_FAIL);

    random_values[0] = 0U;
    random_values[1] = UINT32_C(0xaabbccdd);
    assert(carelink_security_new_session_id() == UINT32_C(0xaabbccdd));
    assert(random_index == 2U);
}

static void test_replay_windows_and_reboot(void)
{
    reset_mock();
    carelink_security_t security;
    assert(carelink_security_init(&security) == ESP_OK);
    uint64_t counter = 0U;
    for (uint64_t expected = 1U; expected <= 4U; ++expected) {
        assert(carelink_security_next_replay_counter(&security, &counter) ==
               ESP_OK);
        assert(counter == expected);
    }
    assert(commits == 1U && committed_high == 4U);
    assert(carelink_security_next_replay_counter(&security, &counter) ==
           ESP_OK);
    assert(counter == 5U && commits == 2U && committed_high == 8U);

    carelink_security_t rebooted;
    assert(carelink_security_init(&rebooted) == ESP_OK);
    assert(carelink_security_next_replay_counter(&rebooted, &counter) ==
           ESP_OK);
    assert(counter == 9U && committed_high == 12U);

    carelink_security_t overflow;
    assert(carelink_security_init(&overflow) == ESP_OK);
    committed_high = UINT64_MAX - 3U;
    pending_high_valid = false;
    assert(carelink_security_next_replay_counter(&overflow, &counter) ==
           ESP_ERR_INVALID_STATE);
}

static void test_api_errors(void)
{
    reset_mock();
    carelink_security_t security = {0};
    uint64_t counter;
    assert(carelink_security_init(NULL) == ESP_ERR_INVALID_ARG);
    assert(carelink_security_next_replay_counter(NULL, &counter) ==
           ESP_ERR_INVALID_ARG);
    assert(carelink_security_next_replay_counter(&security, &counter) ==
           ESP_ERR_INVALID_STATE);
    assert(carelink_security_device_id(NULL) == ESP_ERR_INVALID_ARG);

    assert(carelink_security_init(&security) == ESP_OK);
    commit_fails = true;
    assert(carelink_security_next_replay_counter(&security, &counter) ==
           ESP_FAIL);
}

int main(void)
{
    test_key_provider_fails_closed();
    test_identity_and_session();
    test_replay_windows_and_reboot();
    test_api_errors();
    puts("security storage tests passed");
    return 0;
}
