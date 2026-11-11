#include "carelink_incident_policy.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool active_state(carelink_sos_state_t state)
{
    return state >= CARELINK_SOS_ACTIVE_UNCONFIRMED &&
           state <= CARELINK_SOS_CAREGIVER_ACKNOWLEDGED;
}

static bool map_trigger(carelink_sos_trigger_t source,
                        carelink_trigger_t *destination)
{
    if (destination == NULL) {
        return false;
    }
    switch (source) {
    case CARELINK_SOS_TRIGGER_MANUAL:
        *destination = CARELINK_TRIGGER_MANUAL;
        return true;
    case CARELINK_SOS_TRIGGER_FALL:
        *destination = CARELINK_TRIGGER_FALL;
        return true;
    case CARELINK_SOS_TRIGGER_REMOTE_TEST:
        *destination = CARELINK_TRIGGER_REMOTE_TEST;
        return true;
    default:
        return false;
    }
}

static carelink_delivery_t delivery_for_state(carelink_sos_state_t state)
{
    switch (state) {
    case CARELINK_SOS_RADIO_RECEIVED:
        return CARELINK_DELIVERY_RADIO_RECEIVED;
    case CARELINK_SOS_CLOUD_ACCEPTED:
        return CARELINK_DELIVERY_CLOUD_ACCEPTED;
    case CARELINK_SOS_CAREGIVER_ACKNOWLEDGED:
        return CARELINK_DELIVERY_CAREGIVER_ACKNOWLEDGED;
    default:
        return CARELINK_DELIVERY_ACTIVE_UNCONFIRMED;
    }
}

carelink_incident_policy_result_t carelink_incident_build_message(
    const carelink_sos_snapshot_t *snapshot,
    const carelink_incident_telemetry_t *telemetry,
    carelink_protocol_message_t *message)
{
    if (snapshot == NULL || telemetry == NULL || message == NULL) {
        return CARELINK_INCIDENT_POLICY_INVALID_ARGUMENT;
    }
    if ((!active_state(snapshot->state) &&
         !(snapshot->state == CARELINK_SOS_CANCELLED &&
           snapshot->cancel_delivery_pending)) ||
        snapshot->incident_id == 0U || snapshot->sequence == 0U) {
        return CARELINK_INCIDENT_POLICY_NOT_TRANSMITTABLE;
    }
    if ((!telemetry->has_location &&
         (telemetry->gnss == CARELINK_GNSS_FRESH ||
          telemetry->gnss == CARELINK_GNSS_STALE)) ||
        (telemetry->has_location &&
         telemetry->gnss != CARELINK_GNSS_FRESH &&
         telemetry->gnss != CARELINK_GNSS_STALE)) {
        return CARELINK_INCIDENT_POLICY_INVALID_TELEMETRY;
    }

    carelink_trigger_t trigger;
    if (!map_trigger(snapshot->trigger, &trigger)) {
        return CARELINK_INCIDENT_POLICY_NOT_TRANSMITTABLE;
    }

    carelink_protocol_message_t output = {
        .type = snapshot->state == CARELINK_SOS_CANCELLED
                    ? CARELINK_MESSAGE_SOS_CANCEL
                    : (snapshot->sequence == 1U
                           ? CARELINK_MESSAGE_SOS_START
                           : CARELINK_MESSAGE_SOS_LOCATION_UPDATE),
        .device_id = telemetry->device_id,
        .incident_id = snapshot->incident_id,
        .sequence = snapshot->sequence,
        .session_id = telemetry->session_id,
        .replay_counter = telemetry->replay_counter,
        .trigger = trigger,
        .delivery = delivery_for_state(snapshot->state),
        .battery_mv = telemetry->battery_mv,
        .health_flags = telemetry->health_flags,
        .gnss = telemetry->gnss,
        .has_location = telemetry->has_location,
        .latitude_e7 = telemetry->has_location ? telemetry->latitude_e7 : 0,
        .longitude_e7 = telemetry->has_location ? telemetry->longitude_e7 : 0,
        .location_age_s = telemetry->has_location
                              ? telemetry->location_age_s
                              : CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
    };
    if (carelink_protocol_validate(&output) != CARELINK_PROTOCOL_OK) {
        return CARELINK_INCIDENT_POLICY_INVALID_TELEMETRY;
    }
    *message = output;
    return CARELINK_INCIDENT_POLICY_OK;
}

carelink_incident_policy_result_t carelink_incident_match_ack(
    const carelink_sos_snapshot_t *snapshot,
    uint64_t expected_device_id,
    uint32_t expected_session_id,
    const carelink_protocol_message_t *ack,
    bool authenticated,
    carelink_sos_event_t *event)
{
    if (snapshot == NULL || ack == NULL || event == NULL ||
        expected_device_id == 0U) {
        return CARELINK_INCIDENT_POLICY_INVALID_ARGUMENT;
    }
    if (!authenticated) {
        return CARELINK_INCIDENT_POLICY_UNAUTHENTICATED;
    }
    if ((!active_state(snapshot->state) &&
         !(snapshot->state == CARELINK_SOS_CANCELLED &&
           snapshot->cancel_delivery_pending)) ||
        ack->device_id != expected_device_id ||
        ack->session_id != expected_session_id ||
        ack->incident_id != snapshot->incident_id || ack->sequence == 0U ||
        ack->sequence > snapshot->sequence ||
        ack->sequence < snapshot->last_acknowledged_sequence) {
        return CARELINK_INCIDENT_POLICY_ACK_MISMATCH;
    }

    carelink_sos_event_type_t event_type;
    carelink_delivery_t required_delivery;
    switch (ack->type) {
    case CARELINK_MESSAGE_RADIO_ACK:
        event_type = CARELINK_SOS_EVENT_RADIO_RECEIPT;
        required_delivery = CARELINK_DELIVERY_RADIO_RECEIVED;
        break;
    case CARELINK_MESSAGE_CLOUD_ACK:
        event_type = CARELINK_SOS_EVENT_CLOUD_ACCEPTANCE;
        required_delivery = CARELINK_DELIVERY_CLOUD_ACCEPTED;
        break;
    case CARELINK_MESSAGE_CAREGIVER_ACK:
        event_type = CARELINK_SOS_EVENT_CAREGIVER_ACKNOWLEDGEMENT;
        required_delivery = CARELINK_DELIVERY_CAREGIVER_ACKNOWLEDGED;
        break;
    default:
        return CARELINK_INCIDENT_POLICY_ACK_MISMATCH;
    }
    const carelink_sos_state_t acknowledgement_state =
        event_type == CARELINK_SOS_EVENT_RADIO_RECEIPT
            ? CARELINK_SOS_RADIO_RECEIVED
            : (event_type == CARELINK_SOS_EVENT_CLOUD_ACCEPTANCE
                   ? CARELINK_SOS_CLOUD_ACCEPTED
                   : CARELINK_SOS_CAREGIVER_ACKNOWLEDGED);
    carelink_trigger_t expected_trigger;
    if (!map_trigger(snapshot->trigger, &expected_trigger) ||
        ack->trigger != expected_trigger || ack->delivery != required_delivery ||
        carelink_protocol_validate(ack) != CARELINK_PROTOCOL_OK ||
        (snapshot->state >= acknowledgement_state &&
         ack->sequence <= snapshot->last_acknowledged_sequence)) {
        return CARELINK_INCIDENT_POLICY_ACK_MISMATCH;
    }
    if (snapshot->state == CARELINK_SOS_CANCELLED &&
        (event_type != CARELINK_SOS_EVENT_RADIO_RECEIPT ||
         ack->sequence != snapshot->sequence)) {
        return CARELINK_INCIDENT_POLICY_ACK_MISMATCH;
    }

    *event = (carelink_sos_event_t){
        .type = event_type,
        .incident_id = ack->incident_id,
        .acknowledged_sequence = ack->sequence,
    };
    return CARELINK_INCIDENT_POLICY_OK;
}

bool carelink_retry_config_valid(const carelink_retry_config_t *config)
{
    return config != NULL && config->first_delay_ms > 0U &&
           config->first_delay_ms <= config->maximum_delay_ms &&
           config->maximum_delay_ms <= (uint32_t)INT32_MAX &&
           config->maximum_attempts > 0U;
}

void carelink_retry_reset(carelink_retry_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

bool carelink_retry_record_attempt(carelink_retry_state_t *state,
                                   const carelink_retry_config_t *config,
                                   uint32_t now_ms)
{
    if (state == NULL || !carelink_retry_config_valid(config) ||
        state->attempts >= config->maximum_attempts) {
        return false;
    }
    ++state->attempts;
    if (state->attempts >= config->maximum_attempts) {
        state->scheduled = false;
        return true;
    }

    uint32_t delay = config->first_delay_ms;
    for (uint16_t exponent = 1U; exponent < state->attempts; ++exponent) {
        if (delay >= config->maximum_delay_ms - delay) {
            delay = config->maximum_delay_ms;
            break;
        }
        delay *= 2U;
    }
    if (delay > config->maximum_delay_ms) {
        delay = config->maximum_delay_ms;
    }
    state->next_attempt_ms = now_ms + delay;
    state->scheduled = true;
    return true;
}

bool carelink_retry_due(const carelink_retry_state_t *state, uint32_t now_ms)
{
    return state != NULL && state->scheduled &&
           (int32_t)(now_ms - state->next_attempt_ms) >= 0;
}
