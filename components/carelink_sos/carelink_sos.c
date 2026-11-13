#include "carelink_sos.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool valid_state(carelink_sos_state_t state)
{
    return state >= CARELINK_SOS_IDLE && state <= CARELINK_SOS_CANCELLED;
}

static bool valid_trigger(carelink_sos_trigger_t trigger)
{
    return trigger >= CARELINK_SOS_TRIGGER_MANUAL &&
           trigger <= CARELINK_SOS_TRIGGER_CALL_REQUEST;
}

static bool active_state(carelink_sos_state_t state)
{
    return state >= CARELINK_SOS_ACTIVE_UNCONFIRMED &&
           state <= CARELINK_SOS_CAREGIVER_ACKNOWLEDGED;
}

static bool terminal_state(carelink_sos_state_t state)
{
    return state == CARELINK_SOS_RESOLVED || state == CARELINK_SOS_CANCELLED;
}

static carelink_sos_action_t action_for(const carelink_sos_t *sos,
                                        carelink_sos_result_t result,
                                        bool transmit)
{
    carelink_sos_action_t action = {
        .result = result,
        .transmit = transmit,
    };
    if (sos != NULL) {
        action.incident_id = sos->snapshot.incident_id;
        action.sequence = sos->snapshot.sequence;
        action.state = sos->snapshot.state;
    }
    return action;
}

static bool valid_snapshot(const carelink_sos_snapshot_t *snapshot)
{
    if (snapshot == NULL || !valid_state(snapshot->state)) {
        return false;
    }
    if (snapshot->state == CARELINK_SOS_IDLE) {
        return snapshot->incident_id == 0U && snapshot->sequence == 0U &&
               snapshot->last_acknowledged_sequence == 0U &&
               snapshot->last_local_event_token == 0U &&
               !snapshot->cancel_delivery_pending;
    }
    if (snapshot->state == CARELINK_SOS_ARMING) {
        return snapshot->incident_id == 0U && snapshot->sequence == 0U &&
               snapshot->last_acknowledged_sequence == 0U &&
               snapshot->last_local_event_token != 0U &&
               !snapshot->cancel_delivery_pending;
    }
    if (!valid_trigger(snapshot->trigger) || snapshot->incident_id == 0U ||
        snapshot->sequence == 0U || snapshot->last_local_event_token == 0U ||
        snapshot->last_acknowledged_sequence > snapshot->sequence) {
        return false;
    }
    if (snapshot->state == CARELINK_SOS_ACTIVE_UNCONFIRMED &&
        snapshot->last_acknowledged_sequence != 0U) {
        return false;
    }
    if (snapshot->state >= CARELINK_SOS_RADIO_RECEIVED &&
        snapshot->state <= CARELINK_SOS_RESOLVED &&
        snapshot->last_acknowledged_sequence == 0U) {
        return false;
    }
    if (snapshot->state != CARELINK_SOS_CANCELLED &&
        snapshot->cancel_delivery_pending) {
        return false;
    }
    if (snapshot->state == CARELINK_SOS_CANCELLED &&
        ((snapshot->cancel_delivery_pending &&
          snapshot->last_acknowledged_sequence >= snapshot->sequence) ||
         (!snapshot->cancel_delivery_pending &&
          snapshot->last_acknowledged_sequence != snapshot->sequence))) {
        return false;
    }
    return true;
}

void carelink_sos_init(carelink_sos_t *sos)
{
    if (sos != NULL) {
        memset(sos, 0, sizeof(*sos));
        sos->snapshot.state = CARELINK_SOS_IDLE;
    }
}

carelink_sos_result_t carelink_sos_restore(
    carelink_sos_t *sos,
    const carelink_sos_snapshot_t *persisted,
    carelink_sos_action_t *restored_action)
{
    if (sos == NULL || persisted == NULL || restored_action == NULL) {
        return CARELINK_SOS_INVALID_ARGUMENT;
    }
    if (!valid_snapshot(persisted)) {
        *restored_action = action_for(sos, CARELINK_SOS_INVALID_RESTORE, false);
        return CARELINK_SOS_INVALID_RESTORE;
    }
    sos->snapshot = *persisted;
    *restored_action = action_for(sos, CARELINK_SOS_APPLIED,
                                  active_state(persisted->state) ||
                                      persisted->cancel_delivery_pending);
    return CARELINK_SOS_APPLIED;
}

static carelink_sos_action_t apply_local_transmission(
    carelink_sos_t *sos,
    const carelink_sos_event_t *event,
    carelink_sos_state_t next_state)
{
    if (event->local_event_token == 0U) {
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
    if (event->local_event_token == sos->snapshot.last_local_event_token) {
        return action_for(sos, CARELINK_SOS_DUPLICATE, false);
    }
    if (event->local_event_token < sos->snapshot.last_local_event_token ||
        sos->snapshot.sequence == UINT32_MAX) {
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
    ++sos->snapshot.sequence;
    sos->snapshot.last_local_event_token = event->local_event_token;
    sos->snapshot.state = next_state;
    sos->snapshot.cancel_delivery_pending =
        next_state == CARELINK_SOS_CANCELLED;
    return action_for(sos, CARELINK_SOS_APPLIED, true);
}

static carelink_sos_action_t apply_receipt(carelink_sos_t *sos,
                                           const carelink_sos_event_t *event,
                                           carelink_sos_state_t tier)
{
    if (!active_state(sos->snapshot.state) ||
        event->incident_id != sos->snapshot.incident_id ||
        event->acknowledged_sequence == 0U ||
        event->acknowledged_sequence > sos->snapshot.sequence) {
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
    if (sos->snapshot.state >= tier) {
        if (event->acknowledged_sequence <=
            sos->snapshot.last_acknowledged_sequence) {
            return action_for(sos, CARELINK_SOS_DUPLICATE, false);
        }
        sos->snapshot.last_acknowledged_sequence =
            event->acknowledged_sequence;
        return action_for(sos, CARELINK_SOS_APPLIED, false);
    }
    sos->snapshot.state = tier;
    if (event->acknowledged_sequence >
        sos->snapshot.last_acknowledged_sequence) {
        sos->snapshot.last_acknowledged_sequence =
            event->acknowledged_sequence;
    }
    return action_for(sos, CARELINK_SOS_APPLIED, false);
}

static carelink_sos_action_t apply_trigger(carelink_sos_t *sos,
                                           const carelink_sos_event_t *event)
{
    if (event->incident_id == 0U || event->local_event_token == 0U ||
        !valid_trigger(event->trigger)) {
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
    if (sos->snapshot.state != CARELINK_SOS_ARMING &&
        sos->snapshot.state != CARELINK_SOS_IDLE) {
        if (event->incident_id == sos->snapshot.incident_id &&
            event->local_event_token ==
                sos->snapshot.last_local_event_token) {
            return action_for(sos, CARELINK_SOS_DUPLICATE, false);
        }
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
    if (sos->snapshot.state == CARELINK_SOS_ARMING &&
        event->local_event_token <= sos->snapshot.last_local_event_token) {
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
    sos->snapshot.state = CARELINK_SOS_ACTIVE_UNCONFIRMED;
    sos->snapshot.trigger = event->trigger;
    sos->snapshot.incident_id = event->incident_id;
    sos->snapshot.last_local_event_token = event->local_event_token;
    sos->snapshot.sequence = 1U;
    sos->snapshot.last_acknowledged_sequence = 0U;
    return action_for(sos, CARELINK_SOS_APPLIED, true);
}

carelink_sos_action_t carelink_sos_apply(carelink_sos_t *sos,
                                         const carelink_sos_event_t *event)
{
    if (sos == NULL || event == NULL || !valid_state(sos->snapshot.state)) {
        return action_for(sos, CARELINK_SOS_INVALID_ARGUMENT, false);
    }
    switch (event->type) {
    case CARELINK_SOS_EVENT_ARM:
        if (sos->snapshot.state == CARELINK_SOS_ARMING) {
            return action_for(sos, CARELINK_SOS_DUPLICATE, false);
        }
        if (sos->snapshot.state != CARELINK_SOS_IDLE ||
            event->local_event_token == 0U) {
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        sos->snapshot.state = CARELINK_SOS_ARMING;
        sos->snapshot.last_local_event_token = event->local_event_token;
        return action_for(sos, CARELINK_SOS_APPLIED, false);

    case CARELINK_SOS_EVENT_ABORT_ARMING:
        if (sos->snapshot.state == CARELINK_SOS_IDLE) {
            return action_for(sos, CARELINK_SOS_DUPLICATE, false);
        }
        if (sos->snapshot.state != CARELINK_SOS_ARMING) {
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        carelink_sos_init(sos);
        return action_for(sos, CARELINK_SOS_APPLIED, false);

    case CARELINK_SOS_EVENT_TRIGGER:
        return apply_trigger(sos, event);

    case CARELINK_SOS_EVENT_LOCATION_UPDATE:
        if (!active_state(sos->snapshot.state) ||
            event->incident_id != sos->snapshot.incident_id) {
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        return apply_local_transmission(sos, event, sos->snapshot.state);

    case CARELINK_SOS_EVENT_RETRY:
        if ((!active_state(sos->snapshot.state) &&
             !sos->snapshot.cancel_delivery_pending) ||
            event->incident_id != sos->snapshot.incident_id) {
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        return action_for(sos, CARELINK_SOS_APPLIED, true);

    case CARELINK_SOS_EVENT_RADIO_RECEIPT:
        if (sos->snapshot.state == CARELINK_SOS_CANCELLED) {
            if (event->incident_id != sos->snapshot.incident_id ||
                event->acknowledged_sequence != sos->snapshot.sequence) {
                return action_for(sos, CARELINK_SOS_REJECTED, false);
            }
            if (!sos->snapshot.cancel_delivery_pending) {
                return action_for(sos, CARELINK_SOS_DUPLICATE, false);
            }
            sos->snapshot.cancel_delivery_pending = false;
            sos->snapshot.last_acknowledged_sequence =
                event->acknowledged_sequence;
            return action_for(sos, CARELINK_SOS_APPLIED, false);
        }
        return apply_receipt(sos, event, CARELINK_SOS_RADIO_RECEIVED);

    case CARELINK_SOS_EVENT_CLOUD_ACCEPTANCE:
        return apply_receipt(sos, event, CARELINK_SOS_CLOUD_ACCEPTED);

    case CARELINK_SOS_EVENT_CAREGIVER_ACKNOWLEDGEMENT:
        return apply_receipt(sos, event,
                             CARELINK_SOS_CAREGIVER_ACKNOWLEDGED);

    case CARELINK_SOS_EVENT_CANCEL:
        if (!active_state(sos->snapshot.state) ||
            event->incident_id != sos->snapshot.incident_id) {
            if (sos->snapshot.state == CARELINK_SOS_CANCELLED &&
                event->incident_id == sos->snapshot.incident_id &&
                event->local_event_token ==
                    sos->snapshot.last_local_event_token) {
                return action_for(sos, CARELINK_SOS_DUPLICATE, false);
            }
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        return apply_local_transmission(sos, event, CARELINK_SOS_CANCELLED);

    case CARELINK_SOS_EVENT_RESOLVE:
        if (sos->snapshot.state == CARELINK_SOS_RESOLVED &&
            event->incident_id == sos->snapshot.incident_id) {
            return action_for(sos, CARELINK_SOS_DUPLICATE, false);
        }
        if (sos->snapshot.state != CARELINK_SOS_CAREGIVER_ACKNOWLEDGED ||
            event->incident_id != sos->snapshot.incident_id) {
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        sos->snapshot.state = CARELINK_SOS_RESOLVED;
        return action_for(sos, CARELINK_SOS_APPLIED, false);

    case CARELINK_SOS_EVENT_CLEAR:
        if (!terminal_state(sos->snapshot.state) ||
            sos->snapshot.cancel_delivery_pending) {
            return action_for(sos, CARELINK_SOS_REJECTED, false);
        }
        carelink_sos_init(sos);
        return action_for(sos, CARELINK_SOS_APPLIED, false);

    default:
        return action_for(sos, CARELINK_SOS_REJECTED, false);
    }
}

const carelink_sos_snapshot_t *carelink_sos_snapshot(
    const carelink_sos_t *sos)
{
    return sos == NULL ? NULL : &sos->snapshot;
}

const char *carelink_sos_state_name(carelink_sos_state_t state)
{
    static const char *const names[] = {
        "idle",
        "arming",
        "active_unconfirmed",
        "radio_received",
        "cloud_accepted",
        "caregiver_acknowledged",
        "resolved",
        "cancelled",
    };
    return valid_state(state) ? names[state] : "invalid";
}
