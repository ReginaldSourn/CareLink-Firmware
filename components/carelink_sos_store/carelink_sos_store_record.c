#include "carelink_sos_store_record.h"

#include <string.h>

enum {
    OFFSET_MAGIC = 0,
    OFFSET_SCHEMA = 4,
    OFFSET_LENGTH = 6,
    OFFSET_GENERATION = 8,
    OFFSET_STATE = 12,
    OFFSET_TRIGGER = 13,
    OFFSET_FLAGS = 14,
    OFFSET_RESERVED = 15,
    OFFSET_INCIDENT_ID = 16,
    OFFSET_LOCAL_EVENT_TOKEN = 24,
    OFFSET_SEQUENCE = 32,
    OFFSET_ACKNOWLEDGED_SEQUENCE = 36,
    OFFSET_CRC32 = 40,
};

static const uint8_t MAGIC[4] = {'C', 'L', 'S', '1'};
static const uint8_t FLAG_CANCEL_PENDING = UINT8_C(1) << 0;

_Static_assert(OFFSET_CRC32 + 4U == CARELINK_SOS_STORE_RECORD_SIZE,
               "SOS store wire layout must be exact");

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    for (size_t index = 0; index < 4U; ++index) {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

static void put_u64_le(uint8_t *destination, uint64_t value)
{
    for (size_t index = 0; index < 8U; ++index) {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *source)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4U; ++index) {
        value |= (uint32_t)source[index] << (index * 8U);
    }
    return value;
}

static uint64_t get_u64_le(const uint8_t *source)
{
    uint64_t value = 0;
    for (size_t index = 0; index < 8U; ++index) {
        value |= (uint64_t)source[index] << (index * 8U);
    }
    return value;
}

static uint32_t crc32_ieee(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static bool valid_state(carelink_sos_state_t state)
{
    return state >= CARELINK_SOS_IDLE && state <= CARELINK_SOS_CANCELLED;
}

static bool valid_trigger(carelink_sos_trigger_t trigger)
{
    return trigger >= CARELINK_SOS_TRIGGER_MANUAL &&
           trigger <= CARELINK_SOS_TRIGGER_CALL_REQUEST;
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

carelink_sos_store_record_result_t carelink_sos_store_record_encode(
    const carelink_sos_snapshot_t *snapshot,
    uint32_t generation,
    uint8_t record[CARELINK_SOS_STORE_RECORD_SIZE])
{
    if (snapshot == NULL || record == NULL) {
        return CARELINK_SOS_STORE_RECORD_INVALID_ARGUMENT;
    }
    if (!valid_snapshot(snapshot)) {
        return CARELINK_SOS_STORE_RECORD_INVALID_FIELD;
    }

    memset(record, 0, CARELINK_SOS_STORE_RECORD_SIZE);
    memcpy(&record[OFFSET_MAGIC], MAGIC, sizeof(MAGIC));
    put_u16_le(&record[OFFSET_SCHEMA], CARELINK_SOS_STORE_SCHEMA_VERSION);
    put_u16_le(&record[OFFSET_LENGTH], CARELINK_SOS_STORE_RECORD_SIZE);
    put_u32_le(&record[OFFSET_GENERATION], generation);
    record[OFFSET_STATE] = (uint8_t)snapshot->state;
    record[OFFSET_TRIGGER] = (uint8_t)snapshot->trigger;
    record[OFFSET_FLAGS] =
        snapshot->cancel_delivery_pending ? FLAG_CANCEL_PENDING : 0U;
    put_u64_le(&record[OFFSET_INCIDENT_ID], snapshot->incident_id);
    put_u64_le(&record[OFFSET_LOCAL_EVENT_TOKEN],
               snapshot->last_local_event_token);
    put_u32_le(&record[OFFSET_SEQUENCE], snapshot->sequence);
    put_u32_le(&record[OFFSET_ACKNOWLEDGED_SEQUENCE],
               snapshot->last_acknowledged_sequence);
    put_u32_le(&record[OFFSET_CRC32], crc32_ieee(record, OFFSET_CRC32));
    return CARELINK_SOS_STORE_RECORD_OK;
}

carelink_sos_store_record_result_t carelink_sos_store_record_decode(
    const uint8_t *record,
    size_t record_size,
    carelink_sos_snapshot_t *snapshot,
    uint32_t *generation)
{
    if (record == NULL || snapshot == NULL || generation == NULL) {
        return CARELINK_SOS_STORE_RECORD_INVALID_ARGUMENT;
    }
    if (record_size != CARELINK_SOS_STORE_RECORD_SIZE ||
        get_u16_le(&record[OFFSET_LENGTH]) != CARELINK_SOS_STORE_RECORD_SIZE) {
        return CARELINK_SOS_STORE_RECORD_INVALID_SIZE;
    }
    if (memcmp(&record[OFFSET_MAGIC], MAGIC, sizeof(MAGIC)) != 0) {
        return CARELINK_SOS_STORE_RECORD_INVALID_MAGIC;
    }
    if (get_u16_le(&record[OFFSET_SCHEMA]) !=
        CARELINK_SOS_STORE_SCHEMA_VERSION) {
        return CARELINK_SOS_STORE_RECORD_UNSUPPORTED_SCHEMA;
    }
    if (get_u32_le(&record[OFFSET_CRC32]) !=
        crc32_ieee(record, OFFSET_CRC32)) {
        return CARELINK_SOS_STORE_RECORD_INVALID_CRC;
    }
    if ((record[OFFSET_FLAGS] & (uint8_t)~FLAG_CANCEL_PENDING) != 0U ||
        record[OFFSET_RESERVED] != 0U) {
        return CARELINK_SOS_STORE_RECORD_INVALID_FIELD;
    }

    const carelink_sos_snapshot_t decoded = {
        .state = (carelink_sos_state_t)record[OFFSET_STATE],
        .trigger = (carelink_sos_trigger_t)record[OFFSET_TRIGGER],
        .incident_id = get_u64_le(&record[OFFSET_INCIDENT_ID]),
        .last_local_event_token =
            get_u64_le(&record[OFFSET_LOCAL_EVENT_TOKEN]),
        .sequence = get_u32_le(&record[OFFSET_SEQUENCE]),
        .last_acknowledged_sequence =
            get_u32_le(&record[OFFSET_ACKNOWLEDGED_SEQUENCE]),
        .cancel_delivery_pending =
            (record[OFFSET_FLAGS] & FLAG_CANCEL_PENDING) != 0U,
    };
    if (!valid_snapshot(&decoded)) {
        return CARELINK_SOS_STORE_RECORD_INVALID_FIELD;
    }
    *snapshot = decoded;
    *generation = get_u32_le(&record[OFFSET_GENERATION]);
    return CARELINK_SOS_STORE_RECORD_OK;
}

static bool generation_newer(uint32_t candidate, uint32_t reference)
{
    const uint32_t difference = candidate - reference;
    return difference != 0U && difference < UINT32_C(0x80000000);
}

carelink_sos_store_selection_t carelink_sos_store_record_select(
    carelink_sos_store_record_view_t slot_a,
    carelink_sos_store_record_view_t slot_b)
{
    carelink_sos_store_selection_t selection = {
        .result = CARELINK_SOS_STORE_SELECTION_EMPTY,
        .slot = CARELINK_SOS_STORE_SLOT_A,
    };
    if ((slot_a.present && slot_a.data == NULL) ||
        (slot_b.present && slot_b.data == NULL)) {
        selection.result = CARELINK_SOS_STORE_SELECTION_INVALID_ARGUMENT;
        return selection;
    }

    carelink_sos_snapshot_t snapshot_a;
    carelink_sos_snapshot_t snapshot_b;
    uint32_t generation_a = 0U;
    uint32_t generation_b = 0U;
    const bool valid_a =
        slot_a.present &&
        carelink_sos_store_record_decode(slot_a.data, slot_a.size,
                                         &snapshot_a, &generation_a) ==
            CARELINK_SOS_STORE_RECORD_OK;
    const bool valid_b =
        slot_b.present &&
        carelink_sos_store_record_decode(slot_b.data, slot_b.size,
                                         &snapshot_b, &generation_b) ==
            CARELINK_SOS_STORE_RECORD_OK;

    if (!valid_a && !valid_b) {
        selection.result = (slot_a.present || slot_b.present)
                               ? CARELINK_SOS_STORE_SELECTION_CORRUPT
                               : CARELINK_SOS_STORE_SELECTION_EMPTY;
        return selection;
    }
    if (valid_b && (!valid_a || generation_newer(generation_b, generation_a))) {
        selection.result = CARELINK_SOS_STORE_SELECTION_VALID;
        selection.slot = CARELINK_SOS_STORE_SLOT_B;
        selection.generation = generation_b;
        selection.snapshot = snapshot_b;
        return selection;
    }
    selection.result = CARELINK_SOS_STORE_SELECTION_VALID;
    selection.slot = CARELINK_SOS_STORE_SLOT_A;
    selection.generation = generation_a;
    selection.snapshot = snapshot_a;
    return selection;
}
