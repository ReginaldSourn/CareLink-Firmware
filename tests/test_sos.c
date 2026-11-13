#include "carelink_sos.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static carelink_sos_event_t event(carelink_sos_event_type_t type)
{
    carelink_sos_event_t value = {
        .type = type,
        .incident_id = UINT64_C(0x123456789abcdef0),
        .local_event_token = UINT64_C(10),
        .acknowledged_sequence = 1U,
        .trigger = CARELINK_SOS_TRIGGER_MANUAL,
    };
    return value;
}

static carelink_sos_action_t apply(carelink_sos_t *sos,
                                   carelink_sos_event_type_t type)
{
    const carelink_sos_event_t value = event(type);
    return carelink_sos_apply(sos, &value);
}

static void activate(carelink_sos_t *sos)
{
    carelink_sos_init(sos);
    carelink_sos_event_t value = event(CARELINK_SOS_EVENT_ARM);
    value.local_event_token = 1U;
    assert(carelink_sos_apply(sos, &value).result == CARELINK_SOS_APPLIED);
    value = event(CARELINK_SOS_EVENT_TRIGGER);
    value.local_event_token = 2U;
    const carelink_sos_action_t action = carelink_sos_apply(sos, &value);
    assert(action.result == CARELINK_SOS_APPLIED);
    assert(action.transmit);
    assert(action.sequence == 1U);
    assert(action.state == CARELINK_SOS_ACTIVE_UNCONFIRMED);
}

static void test_manual_sos_does_not_require_location(void)
{
    carelink_sos_t sos;
    carelink_sos_init(&sos);
    assert(apply(&sos, CARELINK_SOS_EVENT_TRIGGER).result ==
           CARELINK_SOS_APPLIED);
    assert(carelink_sos_snapshot(&sos)->state ==
           CARELINK_SOS_ACTIVE_UNCONFIRMED);
    assert(carelink_sos_snapshot(&sos)->sequence == 1U);
}

static void test_call_request_uses_incident_lifecycle(void)
{
    carelink_sos_t sos;
    carelink_sos_init(&sos);
    carelink_sos_event_t value = event(CARELINK_SOS_EVENT_TRIGGER);
    value.trigger = CARELINK_SOS_TRIGGER_CALL_REQUEST;
    const carelink_sos_action_t action = carelink_sos_apply(&sos, &value);
    assert(action.result == CARELINK_SOS_APPLIED);
    assert(action.transmit);
    assert(carelink_sos_snapshot(&sos)->trigger ==
           CARELINK_SOS_TRIGGER_CALL_REQUEST);
}

static void test_delivery_progression_and_idempotence(void)
{
    carelink_sos_t sos;
    activate(&sos);
    carelink_sos_event_t value = event(CARELINK_SOS_EVENT_TRIGGER);
    value.local_event_token = 2U;
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_DUPLICATE);
    assert(carelink_sos_snapshot(&sos)->sequence == 1U);

    carelink_sos_action_t action = apply(&sos,
                                         CARELINK_SOS_EVENT_RADIO_RECEIPT);
    assert(action.result == CARELINK_SOS_APPLIED);
    assert(action.state == CARELINK_SOS_RADIO_RECEIVED);
    assert(!action.transmit);
    assert(apply(&sos, CARELINK_SOS_EVENT_RADIO_RECEIPT).result ==
           CARELINK_SOS_DUPLICATE);

    action = apply(&sos, CARELINK_SOS_EVENT_CLOUD_ACCEPTANCE);
    assert(action.result == CARELINK_SOS_APPLIED);
    assert(action.state == CARELINK_SOS_CLOUD_ACCEPTED);
    action = apply(&sos, CARELINK_SOS_EVENT_CAREGIVER_ACKNOWLEDGEMENT);
    assert(action.result == CARELINK_SOS_APPLIED);
    assert(action.state == CARELINK_SOS_CAREGIVER_ACKNOWLEDGED);
    action = apply(&sos, CARELINK_SOS_EVENT_RESOLVE);
    assert(action.result == CARELINK_SOS_APPLIED);
    assert(action.state == CARELINK_SOS_RESOLVED);
    assert(apply(&sos, CARELINK_SOS_EVENT_RESOLVE).result ==
           CARELINK_SOS_DUPLICATE);
    assert(apply(&sos, CARELINK_SOS_EVENT_CLEAR).state == CARELINK_SOS_IDLE);
}

static void test_retry_update_and_cancel_sequences(void)
{
    carelink_sos_t sos;
    activate(&sos);
    carelink_sos_action_t action = apply(&sos, CARELINK_SOS_EVENT_RETRY);
    assert(action.result == CARELINK_SOS_APPLIED && action.transmit);
    assert(action.sequence == 1U);

    carelink_sos_event_t value = event(CARELINK_SOS_EVENT_LOCATION_UPDATE);
    value.local_event_token = 3U;
    action = carelink_sos_apply(&sos, &value);
    assert(action.result == CARELINK_SOS_APPLIED && action.transmit);
    assert(action.sequence == 2U);
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_DUPLICATE);
    assert(carelink_sos_snapshot(&sos)->sequence == 2U);
    value.local_event_token = 2U;
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_REJECTED);

    value = event(CARELINK_SOS_EVENT_CANCEL);
    value.local_event_token = 4U;
    action = carelink_sos_apply(&sos, &value);
    assert(action.result == CARELINK_SOS_APPLIED && action.transmit);
    assert(action.state == CARELINK_SOS_CANCELLED);
    assert(action.sequence == 3U);
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_DUPLICATE);
}

static void test_wrong_incident_and_ack_sequence_rejected(void)
{
    carelink_sos_t sos;
    activate(&sos);
    carelink_sos_event_t value = event(CARELINK_SOS_EVENT_RADIO_RECEIPT);
    value.incident_id++;
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_REJECTED);
    value = event(CARELINK_SOS_EVENT_RADIO_RECEIPT);
    value.acknowledged_sequence = 2U;
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_REJECTED);
    value.acknowledged_sequence = 0U;
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_REJECTED);
    assert(carelink_sos_snapshot(&sos)->state ==
           CARELINK_SOS_ACTIVE_UNCONFIRMED);
}

static void test_reboot_restore(void)
{
    carelink_sos_t original;
    activate(&original);
    const carelink_sos_snapshot_t persisted = *carelink_sos_snapshot(&original);

    carelink_sos_t restored;
    carelink_sos_init(&restored);
    carelink_sos_action_t action = {0};
    assert(carelink_sos_restore(&restored, &persisted, &action) ==
           CARELINK_SOS_APPLIED);
    assert(action.transmit);
    assert(action.incident_id == persisted.incident_id);
    assert(action.sequence == persisted.sequence);

    carelink_sos_snapshot_t invalid = persisted;
    invalid.sequence = 0U;
    assert(carelink_sos_restore(&restored, &invalid, &action) ==
           CARELINK_SOS_INVALID_RESTORE);
    assert(carelink_sos_snapshot(&restored)->incident_id ==
           persisted.incident_id);

    invalid = persisted;
    invalid.state = CARELINK_SOS_RESOLVED;
    invalid.last_acknowledged_sequence = invalid.sequence;
    assert(carelink_sos_restore(&restored, &invalid, &action) ==
           CARELINK_SOS_APPLIED);
    assert(!action.transmit);
}

static void test_cancel_survives_reboot_until_radio_receipt(void)
{
    carelink_sos_t sos;
    activate(&sos);
    carelink_sos_event_t value = event(CARELINK_SOS_EVENT_CANCEL);
    value.local_event_token = 3U;
    carelink_sos_action_t action = carelink_sos_apply(&sos, &value);
    assert(action.result == CARELINK_SOS_APPLIED && action.transmit);
    assert(action.state == CARELINK_SOS_CANCELLED);
    const carelink_sos_snapshot_t persisted = *carelink_sos_snapshot(&sos);
    assert(persisted.cancel_delivery_pending);

    carelink_sos_t restored;
    carelink_sos_init(&restored);
    assert(carelink_sos_restore(&restored, &persisted, &action) ==
           CARELINK_SOS_APPLIED);
    assert(action.transmit);
    assert(action.sequence == persisted.sequence);

    value = event(CARELINK_SOS_EVENT_CLEAR);
    assert(carelink_sos_apply(&restored, &value).result ==
           CARELINK_SOS_REJECTED);
    assert(carelink_sos_snapshot(&restored)->cancel_delivery_pending);
    assert(carelink_sos_snapshot(&restored)->incident_id ==
           persisted.incident_id);

    value = event(CARELINK_SOS_EVENT_RETRY);
    value.acknowledged_sequence = 0U;
    action = carelink_sos_apply(&restored, &value);
    assert(action.result == CARELINK_SOS_APPLIED && action.transmit);
    assert(action.sequence == persisted.sequence);

    value = event(CARELINK_SOS_EVENT_RADIO_RECEIPT);
    value.acknowledged_sequence = persisted.sequence - 1U;
    assert(carelink_sos_apply(&restored, &value).result ==
           CARELINK_SOS_REJECTED);
    value.acknowledged_sequence = persisted.sequence;
    assert(carelink_sos_apply(&restored, &value).result ==
           CARELINK_SOS_APPLIED);
    assert(!carelink_sos_snapshot(&restored)->cancel_delivery_pending);
    assert(carelink_sos_apply(&restored, &value).result ==
           CARELINK_SOS_DUPLICATE);
    value = event(CARELINK_SOS_EVENT_RETRY);
    assert(carelink_sos_apply(&restored, &value).result ==
           CARELINK_SOS_REJECTED);

    const carelink_sos_snapshot_t delivered =
        *carelink_sos_snapshot(&restored);
    carelink_sos_init(&sos);
    assert(carelink_sos_restore(&sos, &delivered, &action) ==
           CARELINK_SOS_APPLIED);
    assert(!action.transmit);
    value = event(CARELINK_SOS_EVENT_CLEAR);
    assert(carelink_sos_apply(&sos, &value).result ==
           CARELINK_SOS_APPLIED);
    assert(carelink_sos_snapshot(&sos)->state == CARELINK_SOS_IDLE);
}

static carelink_sos_snapshot_t snapshot_for(carelink_sos_state_t state)
{
    carelink_sos_snapshot_t snapshot = {
        .state = state,
        .trigger = CARELINK_SOS_TRIGGER_MANUAL,
        .incident_id = UINT64_C(0x123456789abcdef0),
        .last_local_event_token = 9U,
        .sequence = 1U,
        .last_acknowledged_sequence = 1U,
    };
    if (state == CARELINK_SOS_IDLE) {
        snapshot = (carelink_sos_snapshot_t){.state = CARELINK_SOS_IDLE};
    } else if (state == CARELINK_SOS_ARMING) {
        snapshot.incident_id = 0U;
        snapshot.sequence = 0U;
        snapshot.last_acknowledged_sequence = 0U;
    } else if (state == CARELINK_SOS_ACTIVE_UNCONFIRMED) {
        snapshot.last_acknowledged_sequence = 0U;
    } else if (state == CARELINK_SOS_CANCELLED) {
        snapshot.last_acknowledged_sequence = 0U;
        snapshot.cancel_delivery_pending = true;
    }
    return snapshot;
}

static void test_complete_state_event_matrix(void)
{
    static const carelink_sos_result_t expected[8][11] = {
        /* IDLE */
        {CARELINK_SOS_APPLIED, CARELINK_SOS_DUPLICATE,
         CARELINK_SOS_APPLIED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED},
        /* ARMING */
        {CARELINK_SOS_DUPLICATE, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED},
        /* ACTIVE_UNCONFIRMED */
        {CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED},
        /* RADIO_RECEIVED */
        {CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_DUPLICATE,
         CARELINK_SOS_APPLIED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED},
        /* CLOUD_ACCEPTED */
        {CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_DUPLICATE,
         CARELINK_SOS_DUPLICATE, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED},
        /* CAREGIVER_ACKNOWLEDGED */
        {CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_DUPLICATE,
         CARELINK_SOS_DUPLICATE, CARELINK_SOS_DUPLICATE,
         CARELINK_SOS_APPLIED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_REJECTED},
        /* RESOLVED */
        {CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_DUPLICATE,
         CARELINK_SOS_APPLIED},
        /* CANCELLED */
        {CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_APPLIED, CARELINK_SOS_APPLIED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED, CARELINK_SOS_REJECTED,
         CARELINK_SOS_REJECTED},
    };
    for (int state_value = CARELINK_SOS_IDLE;
         state_value <= CARELINK_SOS_CANCELLED; ++state_value) {
        for (int event_value = CARELINK_SOS_EVENT_ARM;
             event_value <= CARELINK_SOS_EVENT_CLEAR; ++event_value) {
            carelink_sos_t sos;
            carelink_sos_init(&sos);
            carelink_sos_action_t restored = {0};
            const carelink_sos_snapshot_t snapshot =
                snapshot_for((carelink_sos_state_t)state_value);
            assert(carelink_sos_restore(&sos, &snapshot, &restored) ==
                   CARELINK_SOS_APPLIED);
            carelink_sos_event_t value =
                event((carelink_sos_event_type_t)event_value);
            value.local_event_token = 10U;
            const carelink_sos_action_t action =
                carelink_sos_apply(&sos, &value);
            assert(action.result == expected[state_value][event_value]);
            assert(action.state >= CARELINK_SOS_IDLE);
            assert(action.state <= CARELINK_SOS_CANCELLED);
        }
    }
}

static void test_api_and_names(void)
{
    carelink_sos_t sos;
    carelink_sos_init(&sos);
    carelink_sos_action_t restored_action;
    carelink_sos_snapshot_t invalid = {
        .state = CARELINK_SOS_ARMING,
        .last_local_event_token = 0U,
    };
    assert(carelink_sos_restore(&sos, &invalid, &restored_action) ==
           CARELINK_SOS_INVALID_RESTORE);
    invalid = snapshot_for(CARELINK_SOS_ACTIVE_UNCONFIRMED);
    invalid.last_local_event_token = 0U;
    assert(carelink_sos_restore(&sos, &invalid, &restored_action) ==
           CARELINK_SOS_INVALID_RESTORE);
    assert(carelink_sos_apply(NULL, NULL).result ==
           CARELINK_SOS_INVALID_ARGUMENT);
    assert(carelink_sos_apply(&sos, NULL).result ==
           CARELINK_SOS_INVALID_ARGUMENT);
    assert(carelink_sos_snapshot(NULL) == NULL);
    assert(carelink_sos_restore(NULL, NULL, NULL) ==
           CARELINK_SOS_INVALID_ARGUMENT);
    assert(carelink_sos_state_name(CARELINK_SOS_IDLE)[0] == 'i');
    assert(carelink_sos_state_name((carelink_sos_state_t)99)[0] == 'i');
}

int main(void)
{
    test_manual_sos_does_not_require_location();
    test_call_request_uses_incident_lifecycle();
    test_delivery_progression_and_idempotence();
    test_retry_update_and_cancel_sequences();
    test_wrong_incident_and_ack_sequence_rejected();
    test_reboot_restore();
    test_cancel_survives_reboot_until_radio_receipt();
    test_complete_state_event_matrix();
    test_api_and_names();
    puts("sos tests passed");
    return 0;
}
