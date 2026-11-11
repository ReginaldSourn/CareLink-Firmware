#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "carelink_auth.h"
#include "carelink_incident_policy.h"
#include "carelink_radio.h"
#include "carelink_sos.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*carelink_incident_get_telemetry_fn)(
    void *context,
    carelink_incident_telemetry_t *telemetry);

typedef esp_err_t (*carelink_incident_next_counter_fn)(void *context,
                                                       uint64_t *counter);

/* Runs in the incident owner task after an SOS state transition. The callback
 * must not block. */
typedef void (*carelink_incident_status_changed_fn)(
    carelink_sos_state_t state,
    bool cancellation_pending,
    void *context);

typedef struct {
    uint64_t device_id;
    uint32_t session_id;
    carelink_auth_key_provider_t key_provider;
    carelink_incident_get_telemetry_fn get_telemetry;
    carelink_incident_next_counter_fn next_replay_counter;
    carelink_incident_status_changed_fn status_changed;
    void *status_changed_context;
    carelink_radio_config_t radio;
    carelink_retry_config_t retry;
    uint32_t location_update_ms;
    void *context;
} carelink_incident_config_t;

typedef struct {
    carelink_sos_state_t state;
    esp_err_t last_error;
    uint32_t transmissions;
    uint32_t authenticated_acks;
    uint32_t rejected_packets;
    uint32_t dropped_button_events;
    bool running;
    bool persistence_degraded;
    bool configuration_locked;
} carelink_incident_status_t;

/* NVS flash must already be initialized. Startup validates the key, restores
 * the SOS journal, initializes the radio in its owner task, and then starts the
 * button. Missing credentials, corrupt persistence, or radio failure are fatal. */
esp_err_t carelink_incident_start(const carelink_incident_config_t *config);

carelink_incident_status_t carelink_incident_get_status(void);

#ifdef __cplusplus
}
#endif
