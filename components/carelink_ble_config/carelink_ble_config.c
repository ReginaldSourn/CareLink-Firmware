#include "carelink_ble_config.h"

#include "sdkconfig.h"

#if CONFIG_CARELINK_BLE_CONFIG_ENABLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "carelink_config.h"
#include "carelink_incident.h"
#include "esp_local_ctrl.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_srp.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocomm_ble.h"

enum {
    RESTART_DELAY_MS = 1500,
    SEC2_SALT_SIZE = 16,
    PROPERTY_TYPE_CARELINK_CONFIG = 1,
};

typedef enum {
    SERVICE_STOPPED = 0,
    SERVICE_STARTING,
    SERVICE_STARTED,
    SERVICE_STOPPING,
} service_state_t;

static const char *TAG = "carelink_ble";
static char s_device_name[20];
static char *s_sec2_salt;
static char *s_sec2_verifier;
static int s_sec2_verifier_size;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static service_state_t s_service_state;
static bool s_commit_pending;
static carelink_runtime_config_t s_current_config;

static void release_security2(void)
{
    if (s_sec2_salt != NULL) {
        memset(s_sec2_salt, 0, SEC2_SALT_SIZE);
        free(s_sec2_salt);
        s_sec2_salt = NULL;
    }
    if (s_sec2_verifier != NULL) {
        if (s_sec2_verifier_size > 0) {
            memset(s_sec2_verifier, 0, (size_t)s_sec2_verifier_size);
        }
        free(s_sec2_verifier);
        s_sec2_verifier = NULL;
    }
    s_sec2_verifier_size = 0;
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));

    /* An SOS always takes priority over applying a configuration change. If
     * one starts during this delay, keep the saved settings for the next
     * normal reboot instead of interrupting the incident with a restart. */
    const carelink_incident_status_t incident_status = carelink_incident_get_status();
    if (incident_status.configuration_locked) {
        ESP_LOGW(TAG, "SOS became active; postponing configuration restart");
        portENTER_CRITICAL(&s_state_lock);
        s_commit_pending = false;
        portEXIT_CRITICAL(&s_state_lock);
        vTaskDelete(NULL);
        return;
    }

    esp_restart();
}

static void close_window_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_CARELINK_BLE_WINDOW_SECONDS *
                            1000U));
    portENTER_CRITICAL(&s_state_lock);
    const bool should_stop =
        s_service_state == SERVICE_STARTED && !s_commit_pending;
    if (should_stop) {
        s_service_state = SERVICE_STOPPING;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (should_stop) {
        esp_local_ctrl_stop();
        release_security2();
        portENTER_CRITICAL(&s_state_lock);
        s_service_state = SERVICE_STOPPED;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGI(TAG, "BLE configuration window closed");
    }
    vTaskDelete(NULL);
}

static esp_err_t get_property_values(size_t property_count,
                                     const esp_local_ctrl_prop_t properties[],
                                     esp_local_ctrl_prop_val_t values[],
                                     void *context)
{
    (void)context;
    if (property_count != 1U || properties == NULL || values == NULL ||
        strcmp(properties[0].name, "carelink_config") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *encoded = malloc(CARELINK_CONFIG_WIRE_SIZE);
    if (encoded == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (carelink_config_wire_encode(&s_current_config, encoded) !=
        CARELINK_CONFIG_CODEC_OK) {
        free(encoded);
        return ESP_ERR_INVALID_STATE;
    }
    values[0].data = encoded;
    values[0].size = CARELINK_CONFIG_WIRE_SIZE;
    values[0].free_fn = free;
    return ESP_OK;
}

static esp_err_t set_property_values(
    size_t property_count,
    const esp_local_ctrl_prop_t properties[],
    const esp_local_ctrl_prop_val_t values[],
    void *context)
{
    (void)context;
    if (property_count != 1U || properties == NULL || values == NULL ||
        strcmp(properties[0].name, "carelink_config") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    carelink_runtime_config_t candidate;
    if (carelink_config_wire_decode(values[0].data, values[0].size,
                                    &candidate) !=
        CARELINK_CONFIG_CODEC_OK) {
        ESP_LOGW(TAG, "rejected invalid mobile configuration payload");
        return ESP_ERR_INVALID_ARG;
    }
    if (carelink_incident_get_status().configuration_locked) {
        ESP_LOGW(TAG, "configuration change rejected while SOS is active");
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_state_lock);
    if (s_commit_pending) {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_commit_pending = true;
    portEXIT_CRITICAL(&s_state_lock);

    esp_err_t error = carelink_config_save(&candidate);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&s_state_lock);
        s_commit_pending = false;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGE(TAG, "configuration commit failed: %s",
                 esp_err_to_name(error));
        return error;
    }
    if (xTaskCreate(restart_task, "carelink_cfg_restart", 2048, NULL, 5,
                    NULL) != pdPASS) {
        portENTER_CRITICAL(&s_state_lock);
        s_commit_pending = false;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGE(TAG, "configuration saved but restart task creation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "mobile configuration committed; restart scheduled");
    return ESP_OK;
}

static esp_err_t prepare_security2(protocomm_security2_params_t *parameters)
{
    const char *username = CONFIG_CARELINK_BLE_SEC2_USERNAME;
    const char *password = CONFIG_CARELINK_BLE_SEC2_PASSWORD;
    const size_t username_size = strlen(username);
    const size_t password_size = strlen(password);
    if (username_size == 0U || username_size > 32U ||
        password_size < 12U || password_size > 63U) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = esp_srp_gen_salt_verifier(
        username, (int)username_size, password, (int)password_size,
        &s_sec2_salt, SEC2_SALT_SIZE, &s_sec2_verifier,
        &s_sec2_verifier_size);
    if (error != ESP_OK) {
        return error;
    }
    if (s_sec2_verifier_size <= 0 || s_sec2_verifier_size > UINT16_MAX) {
        release_security2();
        return ESP_ERR_INVALID_SIZE;
    }
    parameters->salt = s_sec2_salt;
    parameters->salt_len = SEC2_SALT_SIZE;
    parameters->verifier = s_sec2_verifier;
    parameters->verifier_len = (uint16_t)s_sec2_verifier_size;
    return ESP_OK;
}

esp_err_t carelink_ble_config_start(void)
{
    static const uint8_t service_uuid[16] = {
        0x4c, 0x68, 0x8a, 0xf2, 0x38, 0x47, 0x4e, 0xb1,
        0x99, 0x4b, 0x21, 0x66, 0x43, 0x4c, 0x01, 0x00,
    };
    static protocomm_security2_params_t security2;

    portENTER_CRITICAL(&s_state_lock);
    if (s_service_state != SERVICE_STOPPED) {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_service_state = SERVICE_STARTING;
    portEXIT_CRITICAL(&s_state_lock);

    carelink_config_source_t source;
    esp_err_t error = carelink_config_load(&s_current_config, &source);
    if (error != ESP_OK) {
        goto fail;
    }
    error = prepare_security2(&security2);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "BLE disabled: unique Security 2 credentials are invalid");
        goto fail;
    }

    uint8_t mac[6];
    error = esp_read_mac(mac, ESP_MAC_BT);
    if (error != ESP_OK) {
        goto fail;
    }
    snprintf(s_device_name, sizeof(s_device_name), "CareLink-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    protocomm_ble_config_t transport = {0};
    strlcpy(transport.device_name, s_device_name,
            sizeof(transport.device_name));
    memcpy(transport.service_uuid, service_uuid, sizeof(service_uuid));

    const esp_local_ctrl_config_t local_control = {
        .transport = ESP_LOCAL_CTRL_TRANSPORT_BLE,
        .transport_config.ble = &transport,
        .proto_sec = {
            .version = PROTOCOM_SEC2,
            .sec_params = &security2,
        },
        .handlers = {
            .get_prop_values = get_property_values,
            .set_prop_values = set_property_values,
        },
        .max_properties = 1,
    };
    error = esp_local_ctrl_start(&local_control);
    if (error != ESP_OK) {
        goto fail;
    }
    const esp_local_ctrl_prop_t property = {
        .name = "carelink_config",
        .type = PROPERTY_TYPE_CARELINK_CONFIG,
        .size = CARELINK_CONFIG_WIRE_SIZE,
        .flags = 0,
    };
    error = esp_local_ctrl_add_property(&property);
    if (error != ESP_OK) {
        esp_local_ctrl_stop();
        goto fail;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_service_state = SERVICE_STARTED;
    portEXIT_CRITICAL(&s_state_lock);

#if CONFIG_CARELINK_BLE_WINDOW_SECONDS > 0
    if (xTaskCreate(close_window_task, "carelink_ble_window", 2048, NULL, 4,
                    NULL) != pdPASS) {
        esp_local_ctrl_stop();
        error = ESP_ERR_NO_MEM;
        goto fail;
    }
#endif
    ESP_LOGI(TAG, "secure mobile configuration started as %s", s_device_name);
    return ESP_OK;

fail:
    release_security2();
    memset(&security2, 0, sizeof(security2));
    portENTER_CRITICAL(&s_state_lock);
    s_service_state = SERVICE_STOPPED;
    portEXIT_CRITICAL(&s_state_lock);
    return error;
}

#else

esp_err_t carelink_ble_config_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
