#include "carelink_sos_store_record.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static carelink_sos_snapshot_t active_snapshot(void)
{
    const carelink_sos_snapshot_t snapshot = {
        .state = CARELINK_SOS_ACTIVE_UNCONFIRMED,
        .trigger = CARELINK_SOS_TRIGGER_MANUAL,
        .incident_id = UINT64_C(0x0102030405060708),
        .last_local_event_token = UINT64_C(0x1112131415161718),
        .sequence = UINT32_C(0x21222324),
        .last_acknowledged_sequence = 0U,
        .cancel_delivery_pending = false,
    };
    return snapshot;
}

static void assert_snapshot_equal(const carelink_sos_snapshot_t *left,
                                  const carelink_sos_snapshot_t *right)
{
    assert(left->state == right->state);
    assert(left->trigger == right->trigger);
    assert(left->incident_id == right->incident_id);
    assert(left->last_local_event_token == right->last_local_event_token);
    assert(left->sequence == right->sequence);
    assert(left->last_acknowledged_sequence ==
           right->last_acknowledged_sequence);
    assert(left->cancel_delivery_pending == right->cancel_delivery_pending);
}

static void encode(const carelink_sos_snapshot_t *snapshot,
                   uint32_t generation,
                   uint8_t record[CARELINK_SOS_STORE_RECORD_SIZE])
{
    assert(carelink_sos_store_record_encode(snapshot, generation, record) ==
           CARELINK_SOS_STORE_RECORD_OK);
}

static carelink_sos_store_record_view_t view(const uint8_t *record)
{
    const carelink_sos_store_record_view_t result = {
        .present = true,
        .data = record,
        .size = CARELINK_SOS_STORE_RECORD_SIZE,
    };
    return result;
}

static void test_round_trip_and_layout(void)
{
    const carelink_sos_snapshot_t expected = active_snapshot();
    uint8_t record[CARELINK_SOS_STORE_RECORD_SIZE];
    encode(&expected, UINT32_C(0xa1b2c3d4), record);

    assert(memcmp(record, "CLS1", 4U) == 0);
    assert(record[4] == 1U && record[5] == 0U);
    assert(record[6] == CARELINK_SOS_STORE_RECORD_SIZE && record[7] == 0U);
    assert(record[8] == 0xd4U && record[9] == 0xc3U &&
           record[10] == 0xb2U && record[11] == 0xa1U);
    assert(record[16] == 0x08U && record[23] == 0x01U);

    carelink_sos_snapshot_t actual = {0};
    uint32_t generation = 0U;
    assert(carelink_sos_store_record_decode(
               record, sizeof(record), &actual, &generation) ==
           CARELINK_SOS_STORE_RECORD_OK);
    assert(generation == UINT32_C(0xa1b2c3d4));
    assert_snapshot_equal(&expected, &actual);

    carelink_sos_snapshot_t cancelled = expected;
    cancelled.state = CARELINK_SOS_CANCELLED;
    cancelled.sequence++;
    cancelled.cancel_delivery_pending = true;
    encode(&cancelled, 7U, record);
    memset(&actual, 0, sizeof(actual));
    assert(carelink_sos_store_record_decode(
               record, sizeof(record), &actual, &generation) ==
           CARELINK_SOS_STORE_RECORD_OK);
    assert_snapshot_equal(&cancelled, &actual);
}

static void test_corruption_and_invalid_records(void)
{
    const carelink_sos_snapshot_t snapshot = active_snapshot();
    uint8_t pristine[CARELINK_SOS_STORE_RECORD_SIZE];
    encode(&snapshot, 1U, pristine);

    for (size_t index = 0; index < sizeof(pristine); ++index) {
        uint8_t corrupt[CARELINK_SOS_STORE_RECORD_SIZE];
        memcpy(corrupt, pristine, sizeof(corrupt));
        corrupt[index] ^= UINT8_C(0x80);
        carelink_sos_snapshot_t output = {.incident_id = UINT64_MAX};
        uint32_t generation = UINT32_MAX;
        assert(carelink_sos_store_record_decode(
                   corrupt, sizeof(corrupt), &output, &generation) !=
               CARELINK_SOS_STORE_RECORD_OK);
        assert(output.incident_id == UINT64_MAX);
        assert(generation == UINT32_MAX);
    }

    carelink_sos_snapshot_t output = {0};
    uint32_t generation = 0U;
    assert(carelink_sos_store_record_decode(
               pristine, sizeof(pristine) - 1U, &output, &generation) ==
           CARELINK_SOS_STORE_RECORD_INVALID_SIZE);
    assert(carelink_sos_store_record_decode(NULL, sizeof(pristine), &output,
                                            &generation) ==
           CARELINK_SOS_STORE_RECORD_INVALID_ARGUMENT);

    carelink_sos_snapshot_t invalid = snapshot;
    invalid.last_local_event_token = 0U;
    assert(carelink_sos_store_record_encode(&invalid, 2U, pristine) ==
           CARELINK_SOS_STORE_RECORD_INVALID_FIELD);
    invalid = (carelink_sos_snapshot_t){
        .state = CARELINK_SOS_ARMING,
        .last_local_event_token = 0U,
    };
    assert(carelink_sos_store_record_encode(&invalid, 2U, pristine) ==
           CARELINK_SOS_STORE_RECORD_INVALID_FIELD);
}

static void test_journal_selection_and_wrap(void)
{
    const carelink_sos_snapshot_t snapshot = active_snapshot();
    uint8_t slot_a[CARELINK_SOS_STORE_RECORD_SIZE];
    uint8_t slot_b[CARELINK_SOS_STORE_RECORD_SIZE];
    const carelink_sos_store_record_view_t empty = {0};

    carelink_sos_store_selection_t selected =
        carelink_sos_store_record_select(empty, empty);
    assert(selected.result == CARELINK_SOS_STORE_SELECTION_EMPTY);

    encode(&snapshot, 9U, slot_a);
    selected = carelink_sos_store_record_select(view(slot_a), empty);
    assert(selected.result == CARELINK_SOS_STORE_SELECTION_VALID);
    assert(selected.slot == CARELINK_SOS_STORE_SLOT_A);
    assert(selected.generation == 9U);

    encode(&snapshot, 10U, slot_b);
    selected = carelink_sos_store_record_select(view(slot_a), view(slot_b));
    assert(selected.slot == CARELINK_SOS_STORE_SLOT_B);
    assert(selected.generation == 10U);

    slot_b[20] ^= 1U;
    selected = carelink_sos_store_record_select(view(slot_a), view(slot_b));
    assert(selected.result == CARELINK_SOS_STORE_SELECTION_VALID);
    assert(selected.slot == CARELINK_SOS_STORE_SLOT_A);
    slot_a[21] ^= 1U;
    selected = carelink_sos_store_record_select(view(slot_a), view(slot_b));
    assert(selected.result == CARELINK_SOS_STORE_SELECTION_CORRUPT);

    encode(&snapshot, UINT32_MAX, slot_a);
    encode(&snapshot, 0U, slot_b);
    selected = carelink_sos_store_record_select(view(slot_a), view(slot_b));
    assert(selected.slot == CARELINK_SOS_STORE_SLOT_B);

    encode(&snapshot, 42U, slot_a);
    encode(&snapshot, 42U, slot_b);
    selected = carelink_sos_store_record_select(view(slot_a), view(slot_b));
    assert(selected.slot == CARELINK_SOS_STORE_SLOT_A);

    const carelink_sos_store_record_view_t bad_view = {
        .present = true,
        .data = NULL,
        .size = sizeof(slot_a),
    };
    selected = carelink_sos_store_record_select(bad_view, empty);
    assert(selected.result == CARELINK_SOS_STORE_SELECTION_INVALID_ARGUMENT);
}

int main(void)
{
    test_round_trip_and_layout();
    test_corruption_and_invalid_records();
    test_journal_selection_and_wrap();
    puts("SOS store record tests passed");
    return 0;
}
