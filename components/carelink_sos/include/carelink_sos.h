#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARELINK_SOS_IDLE = 0,
    CARELINK_SOS_ARMING,
    CARELINK_SOS_ACTIVE_UNCONFIRMED,
    CARELINK_SOS_RADIO_RECEIVED,
    CARELINK_SOS_CLOUD_ACCEPTED,
    CARELINK_SOS_CAREGIVER_ACKNOWLEDGED,
    CARELINK_SOS_RESOLVED,
    CARELINK_SOS_CANCELLED,
} carelink_sos_state_t;

typedef enum {
    CARELINK_SOS_TRIGGER_MANUAL = 1,
    CARELINK_SOS_TRIGGER_FALL = 2,
    CARELINK_SOS_TRIGGER_REMOTE_TEST = 3,
} carelink_sos_trigger_t;

typedef enum {
    CARELINK_SOS_EVENT_ARM = 0,
    CARELINK_SOS_EVENT_ABORT_ARMING,
    CARELINK_SOS_EVENT_TRIGGER,
    CARELINK_SOS_EVENT_LOCATION_UPDATE,
    CARELINK_SOS_EVENT_RETRY,
    CARELINK_SOS_EVENT_RADIO_RECEIPT,
    CARELINK_SOS_EVENT_CLOUD_ACCEPTANCE,
    CARELINK_SOS_EVENT_CAREGIVER_ACKNOWLEDGEMENT,
    CARELINK_SOS_EVENT_CANCEL,
    CARELINK_SOS_EVENT_RESOLVE,
    CARELINK_SOS_EVENT_CLEAR,
} carelink_sos_event_type_t;

/* Local event tokens are caller-provided, strictly increasing identifiers
 * within one incident. Replaying the most recent token is idempotent; older
 * tokens are rejected. Receipt events use incident_id and acknowledged_sequence
 * instead. */
typedef struct {
    carelink_sos_event_type_t type;
    uint64_t incident_id;
    uint64_t local_event_token;
    uint32_t acknowledged_sequence;
    carelink_sos_trigger_t trigger;
} carelink_sos_event_t;

/* This POD is the complete persistence record. Store it atomically whenever an
 * event is applied. Restore validates it before making it live. */
typedef struct {
    carelink_sos_state_t state;
    carelink_sos_trigger_t trigger;
    uint64_t incident_id;
    uint64_t last_local_event_token;
    uint32_t sequence;
    uint32_t last_acknowledged_sequence;
    bool cancel_delivery_pending;
} carelink_sos_snapshot_t;

typedef struct {
    carelink_sos_snapshot_t snapshot;
} carelink_sos_t;

typedef enum {
    CARELINK_SOS_APPLIED = 0,
    CARELINK_SOS_DUPLICATE,
    CARELINK_SOS_REJECTED,
    CARELINK_SOS_INVALID_ARGUMENT,
    CARELINK_SOS_INVALID_RESTORE,
} carelink_sos_result_t;

typedef struct {
    carelink_sos_result_t result;
    bool transmit;
    uint64_t incident_id;
    uint32_t sequence;
    carelink_sos_state_t state;
} carelink_sos_action_t;

void carelink_sos_init(carelink_sos_t *sos);

/* Reboot recovery preserves incident and packet identity. For a non-terminal
 * incident, or a CANCELLED incident whose cancellation has not received a
 * radio receipt, restored_action.transmit retries the same sequence. */
carelink_sos_result_t carelink_sos_restore(
    carelink_sos_t *sos,
    const carelink_sos_snapshot_t *persisted,
    carelink_sos_action_t *restored_action);

carelink_sos_action_t carelink_sos_apply(
    carelink_sos_t *sos,
    const carelink_sos_event_t *event);

const carelink_sos_snapshot_t *carelink_sos_snapshot(
    const carelink_sos_t *sos);

const char *carelink_sos_state_name(carelink_sos_state_t state);

#ifdef __cplusplus
}
#endif
