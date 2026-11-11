#include "carelink_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    OFFSET_VERSION = 2,
    OFFSET_TYPE = 3,
    OFFSET_FLAGS = 4,
    OFFSET_PAYLOAD_LENGTH = 5,
    OFFSET_DEVICE_ID = 7,
    OFFSET_INCIDENT_ID = 15,
    OFFSET_SEQUENCE = 23,
    OFFSET_TRIGGER = 39,
    OFFSET_DELIVERY = 40,
    OFFSET_GNSS = 45,
    OFFSET_LATITUDE = 46,
    OFFSET_LONGITUDE = 50,
    OFFSET_LOCATION_AGE = 54,
};

static carelink_protocol_message_t valid_message(bool with_location)
{
    carelink_protocol_message_t message = {
        .type = CARELINK_MESSAGE_SOS_START,
        .device_id = UINT64_C(0x0102030405060708),
        .incident_id = UINT64_C(0x1112131415161718),
        .sequence = UINT32_C(0x21222324),
        .session_id = UINT32_C(0x31323334),
        .replay_counter = UINT64_C(0x4142434445464748),
        .trigger = CARELINK_TRIGGER_MANUAL,
        .delivery = CARELINK_DELIVERY_ACTIVE_UNCONFIRMED,
        .battery_mv = UINT16_C(3850),
        .health_flags = UINT16_C(0x55aa),
        .gnss = with_location ? CARELINK_GNSS_FRESH
                              : CARELINK_GNSS_SEARCHING,
        .has_location = with_location,
        .latitude_e7 = with_location ? INT32_C(115561234) : 0,
        .longitude_e7 = with_location ? INT32_C(1049287654) : 0,
        .location_age_s = with_location
                              ? UINT32_C(3)
                              : CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN,
        .auth_tag = {0, 1, 2, 3, 4, 5, 6, 7,
                     8, 9, 10, 11, 12, 13, 14, 15},
    };
    return message;
}

static void assert_same(const carelink_protocol_message_t *expected,
                        const carelink_protocol_message_t *actual)
{
    assert(actual->type == expected->type);
    assert(actual->device_id == expected->device_id);
    assert(actual->incident_id == expected->incident_id);
    assert(actual->sequence == expected->sequence);
    assert(actual->session_id == expected->session_id);
    assert(actual->replay_counter == expected->replay_counter);
    assert(actual->trigger == expected->trigger);
    assert(actual->delivery == expected->delivery);
    assert(actual->battery_mv == expected->battery_mv);
    assert(actual->health_flags == expected->health_flags);
    assert(actual->gnss == expected->gnss);
    assert(actual->has_location == expected->has_location);
    assert(actual->latitude_e7 == expected->latitude_e7);
    assert(actual->longitude_e7 == expected->longitude_e7);
    assert(actual->location_age_s == expected->location_age_s);
    assert(memcmp(actual->auth_tag, expected->auth_tag,
                  CARELINK_PROTOCOL_AUTH_TAG_SIZE) == 0);
}

static void test_round_trip_and_network_order(void)
{
    const carelink_protocol_message_t message = valid_message(true);
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    size_t size = 0;
    assert(carelink_protocol_encode(&message, frame, sizeof(frame), &size) ==
           CARELINK_PROTOCOL_OK);
    assert(size == CARELINK_PROTOCOL_FRAME_SIZE);
    assert(frame[0] == 'C' && frame[1] == 'L');
    assert(frame[OFFSET_VERSION] == CARELINK_PROTOCOL_VERSION);
    assert(frame[OFFSET_FLAGS] == 1U);
    assert(frame[OFFSET_PAYLOAD_LENGTH] == 0U);
    assert(frame[OFFSET_PAYLOAD_LENGTH + 1] == 67U);
    assert(frame[OFFSET_DEVICE_ID] == 0x01U);
    assert(frame[OFFSET_DEVICE_ID + 7] == 0x08U);
    assert(frame[OFFSET_INCIDENT_ID] == 0x11U);
    assert(frame[OFFSET_SEQUENCE] == 0x21U);
    assert(frame[OFFSET_SEQUENCE + 3] == 0x24U);
    assert(frame[CARELINK_PROTOCOL_AUTHENTICATED_SIZE] == 0U);
    assert(frame[CARELINK_PROTOCOL_FRAME_SIZE - 1U] == 15U);

    carelink_protocol_message_t decoded = {0};
    assert(carelink_protocol_decode(frame, size, &decoded) ==
           CARELINK_PROTOCOL_OK);
    assert_same(&message, &decoded);

    const carelink_protocol_message_t no_location = valid_message(false);
    assert(carelink_protocol_encode(&no_location, frame, sizeof(frame), &size) ==
           CARELINK_PROTOCOL_OK);
    assert(frame[OFFSET_FLAGS] == 0U);
    assert(carelink_protocol_decode(frame, size, &decoded) ==
           CARELINK_PROTOCOL_OK);
    assert_same(&no_location, &decoded);

    carelink_protocol_message_t southern_western = valid_message(true);
    southern_western.latitude_e7 = INT32_C(-338688000);
    southern_western.longitude_e7 = INT32_C(-1512093000);
    assert(carelink_protocol_encode(&southern_western, frame, sizeof(frame),
                                    &size) == CARELINK_PROTOCOL_OK);
    assert(carelink_protocol_decode(frame, size, &decoded) ==
           CARELINK_PROTOCOL_OK);
    assert_same(&southern_western, &decoded);
}

static void test_api_contract(void)
{
    carelink_protocol_message_t message = valid_message(false);
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    size_t size = 123U;
    assert(carelink_protocol_encode(NULL, frame, sizeof(frame), &size) ==
           CARELINK_PROTOCOL_INVALID_ARGUMENT);
    assert(carelink_protocol_encode(&message, NULL, sizeof(frame), &size) ==
           CARELINK_PROTOCOL_INVALID_ARGUMENT);
    assert(carelink_protocol_encode(&message, frame, sizeof(frame), NULL) ==
           CARELINK_PROTOCOL_INVALID_ARGUMENT);
    assert(carelink_protocol_encode(&message, frame, sizeof(frame) - 1U,
                                    &size) ==
           CARELINK_PROTOCOL_BUFFER_TOO_SMALL);
    assert(size == 0U);
    assert(carelink_protocol_decode(NULL, sizeof(frame), &message) ==
           CARELINK_PROTOCOL_INVALID_ARGUMENT);
    assert(carelink_protocol_decode(frame, sizeof(frame), NULL) ==
           CARELINK_PROTOCOL_INVALID_ARGUMENT);
    assert(carelink_protocol_validate(NULL) ==
           CARELINK_PROTOCOL_INVALID_ARGUMENT);
}

static void test_all_wrong_sizes(void)
{
    carelink_protocol_message_t message = valid_message(false);
    carelink_protocol_message_t decoded = {0};
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE + 1U] = {0};
    size_t encoded = 0;
    assert(carelink_protocol_encode(&message, frame, sizeof(frame), &encoded) ==
           CARELINK_PROTOCOL_OK);
    for (size_t size = 0; size < CARELINK_PROTOCOL_FRAME_SIZE; ++size) {
        assert(carelink_protocol_decode(frame, size, &decoded) ==
               CARELINK_PROTOCOL_INVALID_SIZE);
    }
    assert(carelink_protocol_decode(frame, CARELINK_PROTOCOL_FRAME_SIZE + 1U,
                                    &decoded) ==
           CARELINK_PROTOCOL_INVALID_SIZE);
}

static void expect_mutation_rejected(size_t offset, uint8_t value)
{
    carelink_protocol_message_t message = valid_message(false);
    carelink_protocol_message_t decoded = {0};
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    size_t encoded = 0;
    assert(carelink_protocol_encode(&message, frame, sizeof(frame), &encoded) ==
           CARELINK_PROTOCOL_OK);
    frame[offset] = value;
    assert(carelink_protocol_decode(frame, encoded, &decoded) !=
           CARELINK_PROTOCOL_OK);
}

static void expect_zero_range_rejected(size_t offset, size_t count)
{
    carelink_protocol_message_t message = valid_message(false);
    carelink_protocol_message_t decoded = {0};
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    size_t encoded = 0;
    assert(carelink_protocol_encode(&message, frame, sizeof(frame), &encoded) ==
           CARELINK_PROTOCOL_OK);
    memset(&frame[offset], 0, count);
    assert(carelink_protocol_decode(frame, encoded, &decoded) ==
           CARELINK_PROTOCOL_INVALID_FIELD);
}

static void test_malformed_frames(void)
{
    expect_mutation_rejected(0, 'X');
    expect_mutation_rejected(1, 'X');
    expect_mutation_rejected(OFFSET_PAYLOAD_LENGTH, 1U);
    expect_mutation_rejected(OFFSET_PAYLOAD_LENGTH + 1U, 66U);
    expect_zero_range_rejected(OFFSET_DEVICE_ID, 8U);
    expect_zero_range_rejected(OFFSET_INCIDENT_ID, 8U);
    expect_zero_range_rejected(OFFSET_SEQUENCE, 4U);

    for (unsigned value = 0U; value <= UINT8_MAX; ++value) {
        if (value != CARELINK_PROTOCOL_VERSION) {
            expect_mutation_rejected(OFFSET_VERSION, (uint8_t)value);
        }
        if (value != 0U) {
            expect_mutation_rejected(OFFSET_FLAGS, (uint8_t)value);
        }
        const bool known = value >= CARELINK_MESSAGE_SOS_START &&
                           value <= CARELINK_MESSAGE_DEVICE_STATUS;
        if (!known) {
            expect_mutation_rejected(OFFSET_TYPE, (uint8_t)value);
        }
        const bool valid_start_trigger = value >= CARELINK_TRIGGER_MANUAL &&
                                         value <= CARELINK_TRIGGER_REMOTE_TEST;
        if (!valid_start_trigger) {
            expect_mutation_rejected(OFFSET_TRIGGER, (uint8_t)value);
        }
        if (value != CARELINK_DELIVERY_ACTIVE_UNCONFIRMED) {
            expect_mutation_rejected(OFFSET_DELIVERY, (uint8_t)value);
        }
        if (value > CARELINK_GNSS_STALE) {
            expect_mutation_rejected(OFFSET_GNSS, (uint8_t)value);
        }
    }

    /* Absent locations use one canonical representation. */
    expect_mutation_rejected(OFFSET_LATITUDE + 3U, 1U);
    expect_mutation_rejected(OFFSET_LONGITUDE + 3U, 1U);
    expect_mutation_rejected(OFFSET_LOCATION_AGE + 3U, 0U);
}

static void test_semantic_validation(void)
{
    carelink_protocol_message_t message = valid_message(false);
    message.sequence = 0U;
    assert(carelink_protocol_validate(&message) ==
           CARELINK_PROTOCOL_INVALID_FIELD);

    message = valid_message(true);
    message.latitude_e7 = INT32_C(900000001);
    assert(carelink_protocol_validate(&message) ==
           CARELINK_PROTOCOL_INVALID_FIELD);
    message = valid_message(true);
    message.longitude_e7 = INT32_C(-1800000001);
    assert(carelink_protocol_validate(&message) ==
           CARELINK_PROTOCOL_INVALID_FIELD);
    message = valid_message(true);
    message.gnss = CARELINK_GNSS_SEARCHING;
    assert(carelink_protocol_validate(&message) ==
           CARELINK_PROTOCOL_INVALID_FIELD);

    message = valid_message(false);
    message.type = CARELINK_MESSAGE_RADIO_ACK;
    message.delivery = CARELINK_DELIVERY_RADIO_RECEIVED;
    assert(carelink_protocol_validate(&message) == CARELINK_PROTOCOL_OK);
    message.type = CARELINK_MESSAGE_CLOUD_ACK;
    message.delivery = CARELINK_DELIVERY_CLOUD_ACCEPTED;
    assert(carelink_protocol_validate(&message) == CARELINK_PROTOCOL_OK);
    message.type = CARELINK_MESSAGE_CAREGIVER_ACK;
    message.delivery = CARELINK_DELIVERY_CAREGIVER_ACKNOWLEDGED;
    assert(carelink_protocol_validate(&message) == CARELINK_PROTOCOL_OK);
}

int main(void)
{
    test_round_trip_and_network_order();
    test_api_contract();
    test_all_wrong_sizes();
    test_malformed_frames();
    test_semantic_validation();
    puts("protocol tests passed");
    return 0;
}
