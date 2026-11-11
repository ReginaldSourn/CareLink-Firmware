#include "carelink_incident.h"

#include "sdkconfig.h"

#if CONFIG_CARELINK_INCIDENT_ENABLED

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "carelink_button.h"
#include "carelink_protocol.h"
#include "carelink_radio.h"
#include "carelink_sos_store.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum {
    BUTTON_QUEUE_LENGTH = 8,
    LOOP_WAIT_MS = 20,
    HEALTH_TELEMETRY_UNAVAILABLE = UINT16_C(1) << 15,
};

static const char *TAG = "carelink_incident";
static carelink_incident_config_t s_config;
static carelink_sos_t s_sos;
static carelink_sos_store_t s_store;
static carelink_retry_state_t s_retry;
static carelink_incident_status_t s_status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_button_queue;
static StaticQueue_t s_button_queue_control;
static uint8_t s_button_queue_storage[
    BUTTON_QUEUE_LENGTH * sizeof(carelink_button_event_t)];
static StaticTask_t s_task_control;
static StackType_t s_task_stack[
    (CONFIG_CARELINK_INCIDENT_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) /
    sizeof(StackType_t)];
static TaskHandle_t s_start_waiter;
static uint64_t s_local_event_token;
static uint32_t s_next_location_check_ms;
static bool s_location_sent;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool deadline_due(uint32_t deadline_ms)
{
    return (int32_t)(now_ms() - deadline_ms) >= 0;
}

static void status_error(esp_err_t error)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.last_error = error;
    portEXIT_CRITICAL(&s_status_lock);
}

static void status_state(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = s_sos.snapshot.state;
    portEXIT_CRITICAL(&s_status_lock);
}

static bool is_active_or_pending_cancel(void)
{
    return (s_sos.snapshot.state >= CARELINK_SOS_ACTIVE_UNCONFIRMED &&
            s_sos.snapshot.state <= CARELINK_SOS_CAREGIVER_ACKNOWLEDGED) ||
           s_sos.snapshot.cancel_delivery_pending;
}

static esp_err_t persist_snapshot(void)
{
    const esp_err_t error =
        carelink_sos_store_save(&s_store, carelink_sos_snapshot(&s_sos));
    if (error != ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.persistence_degraded = true;
        s_status.last_error = error;
        portEXIT_CRITICAL(&s_status_lock);
        ESP_LOGE(TAG, "SOS journal save failed: %s", esp_err_to_name(error));
    }
    return error;
}

static carelink_retry_config_t retry_config(void)
{
    const carelink_retry_config_t config = {
        .first_delay_ms = CONFIG_CARELINK_INCIDENT_RETRY_FIRST_MS,
        .maximum_delay_ms = CONFIG_CARELINK_INCIDENT_RETRY_MAX_MS,
        .maximum_attempts = CONFIG_CARELINK_INCIDENT_MAX_ATTEMPTS,
    };
    return config;
}

static esp_err_t transmit_current(void)
{
    const carelink_retry_config_t retries = retry_config();
    if (!carelink_retry_record_attempt(&s_retry, &retries, now_ms())) {
        status_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    carelink_incident_telemetry_t telemetry = {
        .device_id = s_config.device_id,
        .session_id = s_config.session_id,
        .gnss = CARELINK_GNSS_UNAVAILABLE,
        .location_age_s = CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
    };
    esp_err_t error = s_config.get_telemetry(s_config.context, &telemetry);
    if (error != ESP_OK) {
        telemetry = (carelink_incident_telemetry_t){
            .device_id = s_config.device_id,
            .session_id = s_config.session_id,
            .health_flags = HEALTH_TELEMETRY_UNAVAILABLE,
            .gnss = CARELINK_GNSS_UNAVAILABLE,
            .location_age_s = CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
        };
        ESP_LOGW(TAG, "telemetry unavailable; sending SOS without sensors");
    }
    telemetry.device_id = s_config.device_id;
    telemetry.session_id = s_config.session_id;
    error = s_config.next_replay_counter(s_config.context,
                                         &telemetry.replay_counter);
    if (error != ESP_OK) {
        status_error(error);
        return error;
    }

    carelink_protocol_message_t message;
    if (carelink_incident_build_message(&s_sos.snapshot, &telemetry,
                                        &message) !=
        CARELINK_INCIDENT_POLICY_OK) {
        status_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE];
    size_t frame_size = 0U;
    if (carelink_protocol_encode(&message, frame, sizeof(frame), &frame_size) !=
        CARELINK_PROTOCOL_OK) {
        status_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (carelink_auth_sign_frame(&s_config.key_provider, frame, frame_size) !=
        CARELINK_AUTH_OK) {
        status_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    error = carelink_radio_send(frame, frame_size,
                                CARELINK_RADIO_AFTER_TX_RECEIVE);
    if (error == ESP_OK && telemetry.has_location) {
        s_location_sent = true;
        s_next_location_check_ms =
            now_ms() + CONFIG_CARELINK_INCIDENT_LOCATION_UPDATE_MS;
    }
    portENTER_CRITICAL(&s_status_lock);
    ++s_status.transmissions;
    s_status.last_error = error;
    portEXIT_CRITICAL(&s_status_lock);
    return error;
}

static void apply_action(carelink_sos_action_t action)
{
    if (action.result != CARELINK_SOS_APPLIED) {
        return;
    }
    status_state();
    (void)persist_snapshot();
    if (action.transmit) {
        (void)transmit_current();
    }
}

static uint64_t new_incident_id(void)
{
    uint64_t value = 0U;
    while (value == 0U) {
        value = ((uint64_t)esp_random() << 32) | esp_random();
    }
    return value;
}

static uint64_t next_local_token(void)
{
    if (s_local_event_token == UINT64_MAX) {
        return 0U;
    }
    return ++s_local_event_token;
}

static void handle_button(const carelink_button_event_t *button)
{
    if (button->type == CARELINK_BUTTON_EVENT_PRESSED &&
        s_sos.snapshot.state == CARELINK_SOS_IDLE) {
        const carelink_sos_event_t event = {
            .type = CARELINK_SOS_EVENT_ARM,
            .local_event_token = next_local_token(),
        };
        apply_action(carelink_sos_apply(&s_sos, &event));
        return;
    }
    if (button->type == CARELINK_BUTTON_EVENT_RELEASED &&
        s_sos.snapshot.state == CARELINK_SOS_ARMING) {
        const carelink_sos_event_t event = {
            .type = CARELINK_SOS_EVENT_ABORT_ARMING,
            .local_event_token = next_local_token(),
        };
        apply_action(carelink_sos_apply(&s_sos, &event));
        return;
    }
    if (button->type != CARELINK_BUTTON_EVENT_SOS_TRIGGERED) {
        return;
    }

    carelink_sos_event_t event = {
        .local_event_token = next_local_token(),
        .trigger = CARELINK_SOS_TRIGGER_MANUAL,
    };
    carelink_retry_reset(&s_retry);
    if (s_sos.snapshot.state == CARELINK_SOS_IDLE ||
        s_sos.snapshot.state == CARELINK_SOS_ARMING) {
        event.type = CARELINK_SOS_EVENT_TRIGGER;
        event.incident_id = new_incident_id();
    } else if (s_sos.snapshot.state >= CARELINK_SOS_ACTIVE_UNCONFIRMED &&
               s_sos.snapshot.state <=
                   CARELINK_SOS_CAREGIVER_ACKNOWLEDGED) {
        event.type = CARELINK_SOS_EVENT_CANCEL;
        event.incident_id = s_sos.snapshot.incident_id;
    } else {
        return;
    }
    apply_action(carelink_sos_apply(&s_sos, &event));
}

static void button_callback(const carelink_button_event_t *event,
                            void *context)
{
    (void)context;
    if (s_button_queue == NULL ||
        xQueueSend(s_button_queue, event, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_status_lock);
        ++s_status.dropped_button_events;
        portEXIT_CRITICAL(&s_status_lock);
    }
}

static void receive_ack(void)
{
    carelink_radio_packet_t packet;
    bool ready = false;
    const esp_err_t error = carelink_radio_receive(&packet, &ready, 0U);
    if (error != ESP_OK) {
        status_error(error);
        return;
    }
    if (!ready) {
        return;
    }
    if (packet.length != CARELINK_PROTOCOL_FRAME_SIZE ||
        carelink_auth_verify_frame(&s_config.key_provider, packet.data,
                                   packet.length) != CARELINK_AUTH_OK) {
        portENTER_CRITICAL(&s_status_lock);
        ++s_status.rejected_packets;
        portEXIT_CRITICAL(&s_status_lock);
        return;
    }
    carelink_protocol_message_t ack;
    if (carelink_protocol_decode(packet.data, packet.length, &ack) !=
        CARELINK_PROTOCOL_OK) {
        portENTER_CRITICAL(&s_status_lock);
        ++s_status.rejected_packets;
        portEXIT_CRITICAL(&s_status_lock);
        return;
    }
    carelink_sos_event_t event;
    if (carelink_incident_match_ack(
            &s_sos.snapshot, s_config.device_id, s_config.session_id, &ack,
            true, &event) != CARELINK_INCIDENT_POLICY_OK) {
        portENTER_CRITICAL(&s_status_lock);
        ++s_status.rejected_packets;
        portEXIT_CRITICAL(&s_status_lock);
        return;
    }
    const carelink_sos_action_t action = carelink_sos_apply(&s_sos, &event);
    if (action.result == CARELINK_SOS_APPLIED) {
        carelink_retry_reset(&s_retry);
        apply_action(action);
        portENTER_CRITICAL(&s_status_lock);
        ++s_status.authenticated_acks;
        portEXIT_CRITICAL(&s_status_lock);
    }
}

static void maybe_send_location_update(void)
{
    if (s_sos.snapshot.state < CARELINK_SOS_RADIO_RECEIVED ||
        s_sos.snapshot.state > CARELINK_SOS_CAREGIVER_ACKNOWLEDGED ||
        !deadline_due(s_next_location_check_ms)) {
        return;
    }

    carelink_incident_telemetry_t telemetry = {
        .gnss = CARELINK_GNSS_UNAVAILABLE,
        .location_age_s = CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
    };
    const esp_err_t error =
        s_config.get_telemetry(s_config.context, &telemetry);
    if (error != ESP_OK || !telemetry.has_location) {
        s_next_location_check_ms = now_ms() + 5000U;
        return;
    }
    const uint64_t token = next_local_token();
    if (token == 0U) {
        status_error(ESP_ERR_INVALID_STATE);
        return;
    }
    const carelink_sos_event_t event = {
        .type = CARELINK_SOS_EVENT_LOCATION_UPDATE,
        .incident_id = s_sos.snapshot.incident_id,
        .local_event_token = token,
    };
    s_next_location_check_ms =
        now_ms() + CONFIG_CARELINK_INCIDENT_LOCATION_UPDATE_MS;
    carelink_retry_reset(&s_retry);
    apply_action(carelink_sos_apply(&s_sos, &event));
}

static esp_err_t initialize_service(carelink_sos_action_t *restored_action)
{
    uint8_t test_frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    if (carelink_auth_sign_frame(&s_config.key_provider, test_frame,
                                 sizeof(test_frame)) != CARELINK_AUTH_OK) {
        ESP_LOGE(TAG, "unique device HMAC key is unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = carelink_sos_store_init(&s_store);
    if (error != ESP_OK) {
        return error;
    }
    carelink_sos_init(&s_sos);
    carelink_sos_snapshot_t saved;
    error = carelink_sos_store_load(&s_store, &saved);
    if (error == ESP_OK) {
        if (carelink_sos_restore(&s_sos, &saved, restored_action) !=
            CARELINK_SOS_APPLIED) {
            return ESP_ERR_INVALID_STATE;
        }
        s_local_event_token = saved.last_local_event_token;
    } else if (error == ESP_ERR_NVS_NOT_FOUND) {
        *restored_action = (carelink_sos_action_t){0};
    } else {
        return error;
    }
    error = carelink_radio_init();
    if (error != ESP_OK) {
        return error;
    }
    error = carelink_radio_start_receive();
    if (error != ESP_OK) {
        return error;
    }
    error = carelink_button_set_callback(button_callback, NULL);
    if (error != ESP_OK) {
        return error;
    }
    return carelink_button_start();
}

static void incident_task(void *argument)
{
    (void)argument;
    carelink_sos_action_t restored_action;
    const esp_err_t startup_error = initialize_service(&restored_action);
    status_error(startup_error);
    if (startup_error == ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.running = true;
        s_status.state = s_sos.snapshot.state;
        portEXIT_CRITICAL(&s_status_lock);
    }
    xTaskNotify(s_start_waiter, (uint32_t)startup_error,
                eSetValueWithOverwrite);
    if (startup_error != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    if (restored_action.transmit) {
        carelink_retry_reset(&s_retry);
        (void)transmit_current();
    }
    if (!s_location_sent) {
        s_next_location_check_ms = now_ms() + 5000U;
    }

    while (true) {
        carelink_button_event_t button;
        if (xQueueReceive(s_button_queue, &button,
                          pdMS_TO_TICKS(LOOP_WAIT_MS)) == pdTRUE) {
            handle_button(&button);
        }
        receive_ack();
        maybe_send_location_update();
        if (is_active_or_pending_cancel() &&
            carelink_retry_due(&s_retry, now_ms())) {
            const carelink_sos_event_t retry = {
                .type = CARELINK_SOS_EVENT_RETRY,
                .incident_id = s_sos.snapshot.incident_id,
            };
            const carelink_sos_action_t action =
                carelink_sos_apply(&s_sos, &retry);
            if (action.result == CARELINK_SOS_APPLIED && action.transmit) {
                (void)transmit_current();
            }
        }
    }
}

esp_err_t carelink_incident_start(const carelink_incident_config_t *config)
{
    if (config == NULL || config->device_id == 0U || config->session_id == 0U ||
        config->key_provider.get_key == NULL || config->get_telemetry == NULL ||
        config->next_replay_counter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_status_lock);
    const bool already_running = s_status.running;
    portEXIT_CRITICAL(&s_status_lock);
    if (already_running || s_button_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const carelink_retry_config_t retries = retry_config();
    if (!carelink_retry_config_valid(&retries)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = CARELINK_SOS_IDLE;
    s_button_queue = xQueueCreateStatic(
        BUTTON_QUEUE_LENGTH, sizeof(carelink_button_event_t),
        s_button_queue_storage, &s_button_queue_control);
    if (s_button_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_start_waiter = xTaskGetCurrentTaskHandle();
    const TaskHandle_t task = xTaskCreateStatic(
        incident_task, "carelink_incident",
        sizeof(s_task_stack) / sizeof(s_task_stack[0]), NULL,
        CONFIG_CARELINK_INCIDENT_TASK_PRIORITY, s_task_stack, &s_task_control);
    if (task == NULL) {
        s_button_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    uint32_t startup_value = (uint32_t)ESP_FAIL;
    if (xTaskNotifyWait(0U, UINT32_MAX, &startup_value,
                        pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return (esp_err_t)startup_value;
}

carelink_incident_status_t carelink_incident_get_status(void)
{
    portENTER_CRITICAL(&s_status_lock);
    const carelink_incident_status_t status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return status;
}

#else

esp_err_t carelink_incident_start(const carelink_incident_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

carelink_incident_status_t carelink_incident_get_status(void)
{
    const carelink_incident_status_t status = {
        .state = CARELINK_SOS_IDLE,
        .last_error = ESP_ERR_NOT_SUPPORTED,
    };
    return status;
}

#endif
