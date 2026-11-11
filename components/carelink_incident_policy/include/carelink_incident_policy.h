#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "carelink_protocol.h"
#include "carelink_sos.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t device_id;
    uint32_t session_id;
    uint64_t replay_counter;
    uint16_t battery_mv;
    uint16_t health_flags;
    carelink_protocol_gnss_t gnss;
    bool has_location;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t location_age_s;
} carelink_incident_telemetry_t;

typedef enum {
    CARELINK_INCIDENT_POLICY_OK = 0,
    CARELINK_INCIDENT_POLICY_INVALID_ARGUMENT,
    CARELINK_INCIDENT_POLICY_NOT_TRANSMITTABLE,
    CARELINK_INCIDENT_POLICY_INVALID_TELEMETRY,
    CARELINK_INCIDENT_POLICY_UNAUTHENTICATED,
    CARELINK_INCIDENT_POLICY_ACK_MISMATCH,
} carelink_incident_policy_result_t;

/* Builds a canonical protocol message from durable incident state. An absent
 * GNSS fix is valid: manual SOS transmission must never wait for position. */
carelink_incident_policy_result_t carelink_incident_build_message(
    const carelink_sos_snapshot_t *snapshot,
    const carelink_incident_telemetry_t *telemetry,
    carelink_protocol_message_t *message);

/* Accepts an ACK only after cryptographic verification and only when it
 * belongs to this device, boot session, incident, and a not-yet-acknowledged
 * transmitted sequence. On success, event is ready for carelink_sos_apply(). */
carelink_incident_policy_result_t carelink_incident_match_ack(
    const carelink_sos_snapshot_t *snapshot,
    uint64_t expected_device_id,
    uint32_t expected_session_id,
    const carelink_protocol_message_t *ack,
    bool authenticated,
    carelink_sos_event_t *event);

typedef struct {
    uint32_t first_delay_ms;
    uint32_t maximum_delay_ms;
    uint16_t maximum_attempts;
} carelink_retry_config_t;

typedef struct {
    uint32_t next_attempt_ms;
    uint16_t attempts;
    bool scheduled;
} carelink_retry_state_t;

bool carelink_retry_config_valid(const carelink_retry_config_t *config);
void carelink_retry_reset(carelink_retry_state_t *state);

/* Records a completed send attempt and schedules the next one with saturated
 * exponential backoff. maximum_attempts includes the initial transmission. */
bool carelink_retry_record_attempt(carelink_retry_state_t *state,
                                   const carelink_retry_config_t *config,
                                   uint32_t now_ms);

/* Wrap-safe for deadlines no farther than INT32_MAX milliseconds away. */
bool carelink_retry_due(const carelink_retry_state_t *state, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
