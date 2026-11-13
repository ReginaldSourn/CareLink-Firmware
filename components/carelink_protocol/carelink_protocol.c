#include "carelink_protocol.h"

#include <string.h>

enum {
    OFFSET_MAGIC = 0,
    OFFSET_VERSION = 2,
    OFFSET_TYPE = 3,
    OFFSET_FLAGS = 4,
    OFFSET_PAYLOAD_LENGTH = 5,
    OFFSET_DEVICE_ID = 7,
    OFFSET_INCIDENT_ID = 15,
    OFFSET_SEQUENCE = 23,
    OFFSET_SESSION_ID = 27,
    OFFSET_REPLAY_COUNTER = 31,
    OFFSET_TRIGGER = 39,
    OFFSET_DELIVERY = 40,
    OFFSET_BATTERY_MV = 41,
    OFFSET_HEALTH_FLAGS = 43,
    OFFSET_GNSS = 45,
    OFFSET_LATITUDE_E7 = 46,
    OFFSET_LONGITUDE_E7 = 50,
    OFFSET_LOCATION_AGE_S = 54,
    OFFSET_AUTH_TAG = 58,
    HEADER_SIZE = 7,
    PAYLOAD_SIZE = CARELINK_PROTOCOL_FRAME_SIZE - HEADER_SIZE,
};

static const uint8_t MAGIC[2] = {'C', 'L'};
static const uint8_t FLAG_HAS_LOCATION = UINT8_C(1) << 0;

_Static_assert(OFFSET_AUTH_TAG == CARELINK_PROTOCOL_AUTHENTICATED_SIZE,
               "authentication boundary must precede the tag");
_Static_assert(OFFSET_AUTH_TAG + CARELINK_PROTOCOL_AUTH_TAG_SIZE ==
                   CARELINK_PROTOCOL_FRAME_SIZE,
               "wire layout must fill exactly one frame");

static void put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void put_u64(uint8_t *destination, uint64_t value)
{
    for (size_t index = 0; index < 8U; ++index) {
        destination[index] = (uint8_t)(value >> (56U - (index * 8U)));
    }
}

static uint16_t get_u16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t get_u32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | (uint32_t)source[3];
}

static uint64_t get_u64(const uint8_t *source)
{
    uint64_t value = 0;
    for (size_t index = 0; index < 8U; ++index) {
        value = (value << 8) | source[index];
    }
    return value;
}

static bool valid_type(carelink_message_type_t type)
{
    return type >= CARELINK_MESSAGE_SOS_START &&
           type <= CARELINK_MESSAGE_CAREGIVER_ACK;
}

static bool valid_trigger(carelink_trigger_t trigger)
{
    return trigger >= CARELINK_TRIGGER_NONE &&
           trigger <= CARELINK_TRIGGER_CALL_REQUEST;
}

static bool valid_delivery(carelink_delivery_t delivery)
{
    return delivery >= CARELINK_DELIVERY_ACTIVE_UNCONFIRMED &&
           delivery <= CARELINK_DELIVERY_CAREGIVER_ACKNOWLEDGED;
}

static bool valid_gnss(carelink_protocol_gnss_t gnss)
{
    return gnss >= CARELINK_GNSS_UNAVAILABLE && gnss <= CARELINK_GNSS_STALE;
}

static bool incident_required(carelink_message_type_t type)
{
    return type != CARELINK_MESSAGE_DEVICE_STATUS;
}

carelink_protocol_result_t carelink_protocol_validate(
    const carelink_protocol_message_t *message)
{
    if (message == NULL) {
        return CARELINK_PROTOCOL_INVALID_ARGUMENT;
    }
    if (!valid_type(message->type) || !valid_trigger(message->trigger) ||
        !valid_delivery(message->delivery) || !valid_gnss(message->gnss) ||
        message->device_id == 0U || message->sequence == 0U ||
        (incident_required(message->type) && message->incident_id == 0U)) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }

    if (message->has_location) {
        if (message->gnss == CARELINK_GNSS_UNAVAILABLE ||
            message->gnss == CARELINK_GNSS_SEARCHING ||
            message->latitude_e7 < INT32_C(-900000000) ||
            message->latitude_e7 > INT32_C(900000000) ||
            message->longitude_e7 < INT32_C(-1800000000) ||
            message->longitude_e7 > INT32_C(1800000000) ||
            message->location_age_s == CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN) {
            return CARELINK_PROTOCOL_INVALID_FIELD;
        }
    } else if (message->latitude_e7 != 0 || message->longitude_e7 != 0 ||
               message->location_age_s != CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }

    if (message->type == CARELINK_MESSAGE_SOS_START &&
        (message->trigger == CARELINK_TRIGGER_NONE ||
         message->delivery != CARELINK_DELIVERY_ACTIVE_UNCONFIRMED)) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }
    if (message->type == CARELINK_MESSAGE_RADIO_ACK &&
        message->delivery != CARELINK_DELIVERY_RADIO_RECEIVED) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }
    if (message->type == CARELINK_MESSAGE_CLOUD_ACK &&
        message->delivery != CARELINK_DELIVERY_CLOUD_ACCEPTED) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }
    if (message->type == CARELINK_MESSAGE_CAREGIVER_ACK &&
        message->delivery != CARELINK_DELIVERY_CAREGIVER_ACKNOWLEDGED) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }
    return CARELINK_PROTOCOL_OK;
}

carelink_protocol_result_t carelink_protocol_encode(
    const carelink_protocol_message_t *message,
    uint8_t *frame,
    size_t capacity,
    size_t *encoded_size)
{
    if (message == NULL || frame == NULL || encoded_size == NULL) {
        return CARELINK_PROTOCOL_INVALID_ARGUMENT;
    }
    *encoded_size = 0U;
    if (capacity < CARELINK_PROTOCOL_FRAME_SIZE) {
        return CARELINK_PROTOCOL_BUFFER_TOO_SMALL;
    }
    const carelink_protocol_result_t validation =
        carelink_protocol_validate(message);
    if (validation != CARELINK_PROTOCOL_OK) {
        return validation;
    }

    memset(frame, 0, CARELINK_PROTOCOL_FRAME_SIZE);
    memcpy(&frame[OFFSET_MAGIC], MAGIC, sizeof(MAGIC));
    frame[OFFSET_VERSION] = CARELINK_PROTOCOL_VERSION;
    frame[OFFSET_TYPE] = (uint8_t)message->type;
    frame[OFFSET_FLAGS] = message->has_location ? FLAG_HAS_LOCATION : 0U;
    put_u16(&frame[OFFSET_PAYLOAD_LENGTH], (uint16_t)PAYLOAD_SIZE);
    put_u64(&frame[OFFSET_DEVICE_ID], message->device_id);
    put_u64(&frame[OFFSET_INCIDENT_ID], message->incident_id);
    put_u32(&frame[OFFSET_SEQUENCE], message->sequence);
    put_u32(&frame[OFFSET_SESSION_ID], message->session_id);
    put_u64(&frame[OFFSET_REPLAY_COUNTER], message->replay_counter);
    frame[OFFSET_TRIGGER] = (uint8_t)message->trigger;
    frame[OFFSET_DELIVERY] = (uint8_t)message->delivery;
    put_u16(&frame[OFFSET_BATTERY_MV], message->battery_mv);
    put_u16(&frame[OFFSET_HEALTH_FLAGS], message->health_flags);
    frame[OFFSET_GNSS] = (uint8_t)message->gnss;
    put_u32(&frame[OFFSET_LATITUDE_E7], (uint32_t)message->latitude_e7);
    put_u32(&frame[OFFSET_LONGITUDE_E7], (uint32_t)message->longitude_e7);
    put_u32(&frame[OFFSET_LOCATION_AGE_S], message->location_age_s);
    memcpy(&frame[OFFSET_AUTH_TAG], message->auth_tag,
           CARELINK_PROTOCOL_AUTH_TAG_SIZE);
    *encoded_size = CARELINK_PROTOCOL_FRAME_SIZE;
    return CARELINK_PROTOCOL_OK;
}

carelink_protocol_result_t carelink_protocol_decode(
    const uint8_t *frame,
    size_t frame_size,
    carelink_protocol_message_t *message)
{
    if (frame == NULL || message == NULL) {
        return CARELINK_PROTOCOL_INVALID_ARGUMENT;
    }
    if (frame_size != CARELINK_PROTOCOL_FRAME_SIZE) {
        return CARELINK_PROTOCOL_INVALID_SIZE;
    }
    if (memcmp(&frame[OFFSET_MAGIC], MAGIC, sizeof(MAGIC)) != 0) {
        return CARELINK_PROTOCOL_INVALID_MAGIC;
    }
    if (frame[OFFSET_VERSION] != CARELINK_PROTOCOL_VERSION) {
        return CARELINK_PROTOCOL_UNSUPPORTED_VERSION;
    }
    if (get_u16(&frame[OFFSET_PAYLOAD_LENGTH]) != PAYLOAD_SIZE) {
        return CARELINK_PROTOCOL_INVALID_LENGTH;
    }
    if ((frame[OFFSET_FLAGS] & (uint8_t)~FLAG_HAS_LOCATION) != 0U) {
        return CARELINK_PROTOCOL_INVALID_FIELD;
    }

    carelink_protocol_message_t decoded = {0};
    decoded.type = (carelink_message_type_t)frame[OFFSET_TYPE];
    decoded.device_id = get_u64(&frame[OFFSET_DEVICE_ID]);
    decoded.incident_id = get_u64(&frame[OFFSET_INCIDENT_ID]);
    decoded.sequence = get_u32(&frame[OFFSET_SEQUENCE]);
    decoded.session_id = get_u32(&frame[OFFSET_SESSION_ID]);
    decoded.replay_counter = get_u64(&frame[OFFSET_REPLAY_COUNTER]);
    decoded.trigger = (carelink_trigger_t)frame[OFFSET_TRIGGER];
    decoded.delivery = (carelink_delivery_t)frame[OFFSET_DELIVERY];
    decoded.battery_mv = get_u16(&frame[OFFSET_BATTERY_MV]);
    decoded.health_flags = get_u16(&frame[OFFSET_HEALTH_FLAGS]);
    decoded.gnss = (carelink_protocol_gnss_t)frame[OFFSET_GNSS];
    decoded.has_location = (frame[OFFSET_FLAGS] & FLAG_HAS_LOCATION) != 0U;
    decoded.latitude_e7 = (int32_t)get_u32(&frame[OFFSET_LATITUDE_E7]);
    decoded.longitude_e7 = (int32_t)get_u32(&frame[OFFSET_LONGITUDE_E7]);
    decoded.location_age_s = get_u32(&frame[OFFSET_LOCATION_AGE_S]);
    memcpy(decoded.auth_tag, &frame[OFFSET_AUTH_TAG],
           CARELINK_PROTOCOL_AUTH_TAG_SIZE);

    const carelink_protocol_result_t validation =
        carelink_protocol_validate(&decoded);
    if (validation != CARELINK_PROTOCOL_OK) {
        return validation;
    }
    *message = decoded;
    return CARELINK_PROTOCOL_OK;
}
