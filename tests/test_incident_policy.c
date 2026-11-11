#include "carelink_incident_policy.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static carelink_sos_snapshot_t active_snapshot(void)
{
    const carelink_sos_snapshot_t snapshot = {
        .state = CARELINK_SOS_ACTIVE_UNCONFIRMED,
        .trigger = CARELINK_SOS_TRIGGER_MANUAL,
        .incident_id = UINT64_C(0x1020304050607080),
        .last_local_event_token = 3U,
        .sequence = 1U,
    };
    return snapshot;
}

static carelink_incident_telemetry_t no_fix_telemetry(void)
{
    const carelink_incident_telemetry_t telemetry = {
        .device_id = UINT64_C(0x0102030405060708),
        .session_id = UINT32_C(0x11223344),
        .replay_counter = UINT64_C(99),
        .battery_mv = 3825U,
        .health_flags = UINT16_C(0x0042),
        .gnss = CARELINK_GNSS_SEARCHING,
        .has_location = false,
        .location_age_s = CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
    };
    return telemetry;
}

static carelink_protocol_message_t radio_ack(
    const carelink_sos_snapshot_t *snapshot,
    const carelink_incident_telemetry_t *telemetry)
{
    const carelink_protocol_message_t ack = {
        .type = CARELINK_MESSAGE_RADIO_ACK,
        .device_id = telemetry->device_id,
        .incident_id = snapshot->incident_id,
        .sequence = snapshot->sequence,
        .session_id = telemetry->session_id,
        .replay_counter = 100U,
        .trigger = CARELINK_TRIGGER_MANUAL,
        .delivery = CARELINK_DELIVERY_RADIO_RECEIVED,
        .gnss = CARELINK_GNSS_UNAVAILABLE,
        .location_age_s = CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
    };
    return ack;
}

static void test_manual_sos_without_gnss(void)
{
    const carelink_sos_snapshot_t snapshot = active_snapshot();
    const carelink_incident_telemetry_t telemetry = no_fix_telemetry();
    carelink_protocol_message_t message;
    memset(&message, 0xa5, sizeof(message));
    assert(carelink_incident_build_message(&snapshot, &telemetry, &message) ==
           CARELINK_INCIDENT_POLICY_OK);
    assert(message.type == CARELINK_MESSAGE_SOS_START);
    assert(message.trigger == CARELINK_TRIGGER_MANUAL);
    assert(message.delivery == CARELINK_DELIVERY_ACTIVE_UNCONFIRMED);
    assert(message.device_id == telemetry.device_id);
    assert(message.incident_id == snapshot.incident_id);
    assert(message.sequence == 1U);
    assert(!message.has_location);
    assert(message.latitude_e7 == 0 && message.longitude_e7 == 0);
    assert(message.location_age_s == CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN);
    for (size_t index = 0; index < sizeof(message.auth_tag); ++index) {
        assert(message.auth_tag[index] == 0U);
    }
}

static void test_location_update_cancel_and_bad_telemetry(void)
{
    carelink_sos_snapshot_t snapshot = active_snapshot();
    snapshot.sequence = 2U;
    carelink_incident_telemetry_t telemetry = no_fix_telemetry();
    telemetry.gnss = CARELINK_GNSS_FRESH;
    telemetry.has_location = true;
    telemetry.latitude_e7 = INT32_C(115561234);
    telemetry.longitude_e7 = INT32_C(1049287654);
    telemetry.location_age_s = 4U;
    carelink_protocol_message_t message = {0};
    assert(carelink_incident_build_message(&snapshot, &telemetry, &message) ==
           CARELINK_INCIDENT_POLICY_OK);
    assert(message.type == CARELINK_MESSAGE_SOS_LOCATION_UPDATE);
    assert(message.has_location && message.location_age_s == 4U);

    snapshot.state = CARELINK_SOS_CANCELLED;
    snapshot.cancel_delivery_pending = true;
    assert(carelink_incident_build_message(&snapshot, &telemetry, &message) ==
           CARELINK_INCIDENT_POLICY_OK);
    assert(message.type == CARELINK_MESSAGE_SOS_CANCEL);

    snapshot.cancel_delivery_pending = false;
    assert(carelink_incident_build_message(&snapshot, &telemetry, &message) ==
           CARELINK_INCIDENT_POLICY_NOT_TRANSMITTABLE);
    snapshot = active_snapshot();
    telemetry.device_id = 0U;
    assert(carelink_incident_build_message(&snapshot, &telemetry, &message) ==
           CARELINK_INCIDENT_POLICY_INVALID_TELEMETRY);
    telemetry = no_fix_telemetry();
    telemetry.gnss = CARELINK_GNSS_FRESH;
    assert(carelink_incident_build_message(&snapshot, &telemetry, &message) ==
           CARELINK_INCIDENT_POLICY_INVALID_TELEMETRY);
}

static void test_authenticated_ack_matching(void)
{
    carelink_sos_snapshot_t snapshot = active_snapshot();
    const carelink_incident_telemetry_t telemetry = no_fix_telemetry();
    carelink_protocol_message_t ack = radio_ack(&snapshot, &telemetry);
    carelink_sos_event_t event = {0};

    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               false, &event) == CARELINK_INCIDENT_POLICY_UNAUTHENTICATED);
    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               true, &event) == CARELINK_INCIDENT_POLICY_OK);
    assert(event.type == CARELINK_SOS_EVENT_RADIO_RECEIPT);
    assert(event.incident_id == snapshot.incident_id);
    assert(event.acknowledged_sequence == snapshot.sequence);

    ack.device_id++;
    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               true, &event) == CARELINK_INCIDENT_POLICY_ACK_MISMATCH);
    ack = radio_ack(&snapshot, &telemetry);
    ack.session_id++;
    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               true, &event) == CARELINK_INCIDENT_POLICY_ACK_MISMATCH);

    ack = radio_ack(&snapshot, &telemetry);
    snapshot.state = CARELINK_SOS_RADIO_RECEIVED;
    snapshot.last_acknowledged_sequence = 1U;
    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               true, &event) == CARELINK_INCIDENT_POLICY_ACK_MISMATCH);

    ack.type = CARELINK_MESSAGE_CLOUD_ACK;
    ack.delivery = CARELINK_DELIVERY_CLOUD_ACCEPTED;
    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               true, &event) == CARELINK_INCIDENT_POLICY_OK);
    assert(event.type == CARELINK_SOS_EVENT_CLOUD_ACCEPTANCE);

    snapshot.state = CARELINK_SOS_CANCELLED;
    snapshot.sequence = 2U;
    snapshot.cancel_delivery_pending = true;
    ack = radio_ack(&snapshot, &telemetry);
    ack.sequence = 1U;
    assert(carelink_incident_match_ack(
               &snapshot, telemetry.device_id, telemetry.session_id, &ack,
               true, &event) == CARELINK_INCIDENT_POLICY_ACK_MISMATCH);
}

static void test_retry_backoff_and_wrap(void)
{
    const carelink_retry_config_t config = {
        .first_delay_ms = 1000U,
        .maximum_delay_ms = 5000U,
        .maximum_attempts = 5U,
    };
    assert(carelink_retry_config_valid(&config));
    carelink_retry_state_t state;
    carelink_retry_reset(&state);
    assert(!carelink_retry_due(&state, UINT32_MAX));

    assert(carelink_retry_record_attempt(&state, &config, 100U));
    assert(state.attempts == 1U && state.next_attempt_ms == 1100U);
    assert(!carelink_retry_due(&state, 1099U));
    assert(carelink_retry_due(&state, 1100U));
    assert(carelink_retry_record_attempt(&state, &config, 1100U));
    assert(state.next_attempt_ms == 3100U);
    assert(carelink_retry_record_attempt(&state, &config, 3100U));
    assert(state.next_attempt_ms == 7100U);
    assert(carelink_retry_record_attempt(&state, &config, 7100U));
    assert(state.next_attempt_ms == 12100U);
    assert(carelink_retry_record_attempt(&state, &config, 12100U));
    assert(state.attempts == 5U && !state.scheduled);
    assert(!carelink_retry_record_attempt(&state, &config, 12100U));

    carelink_retry_reset(&state);
    assert(carelink_retry_record_attempt(&state, &config,
                                         UINT32_MAX - 499U));
    assert(state.next_attempt_ms == 500U);
    assert(!carelink_retry_due(&state, 499U));
    assert(carelink_retry_due(&state, 500U));

    carelink_retry_config_t invalid = config;
    invalid.first_delay_ms = 0U;
    assert(!carelink_retry_config_valid(&invalid));
    invalid = config;
    invalid.maximum_delay_ms = (uint32_t)INT32_MAX + 1U;
    assert(!carelink_retry_config_valid(&invalid));
}

int main(void)
{
    test_manual_sos_without_gnss();
    test_location_update_cancel_and_bad_telemetry();
    test_authenticated_ack_matching();
    test_retry_backoff_and_wrap();
    puts("incident policy tests passed");
    return 0;
}
