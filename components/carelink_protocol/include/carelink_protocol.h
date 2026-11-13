#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_PROTOCOL_VERSION UINT8_C(1)
#define CARELINK_PROTOCOL_AUTH_TAG_SIZE 16U
#define CARELINK_PROTOCOL_FRAME_SIZE 74U
#define CARELINK_PROTOCOL_AUTHENTICATED_SIZE 58U
#define CARELINK_PROTOCOL_LOCATION_AGE_UNKNOWN UINT32_MAX

typedef enum {
    CARELINK_MESSAGE_INVALID = 0,
    CARELINK_MESSAGE_SOS_START = 1,
    CARELINK_MESSAGE_SOS_LOCATION_UPDATE = 2,
    CARELINK_MESSAGE_SOS_CANCEL = 3,
    CARELINK_MESSAGE_DEVICE_STATUS = 4,
    CARELINK_MESSAGE_RADIO_ACK = 5,
    CARELINK_MESSAGE_CLOUD_ACK = 6,
    CARELINK_MESSAGE_CAREGIVER_ACK = 7,
} carelink_message_type_t;

typedef enum {
    CARELINK_TRIGGER_NONE = 0,
    CARELINK_TRIGGER_MANUAL = 1,
    CARELINK_TRIGGER_FALL = 2,
    CARELINK_TRIGGER_REMOTE_TEST = 3,
    CARELINK_TRIGGER_CALL_REQUEST = 4,
} carelink_trigger_t;

typedef enum {
    CARELINK_DELIVERY_ACTIVE_UNCONFIRMED = 0,
    CARELINK_DELIVERY_RADIO_RECEIVED = 1,
    CARELINK_DELIVERY_CLOUD_ACCEPTED = 2,
    CARELINK_DELIVERY_CAREGIVER_ACKNOWLEDGED = 3,
} carelink_delivery_t;

typedef enum {
    CARELINK_GNSS_UNAVAILABLE = 0,
    CARELINK_GNSS_SEARCHING = 1,
    CARELINK_GNSS_FRESH = 2,
    CARELINK_GNSS_STALE = 3,
} carelink_protocol_gnss_t;

typedef struct {
    carelink_message_type_t type;
    uint64_t device_id;
    uint64_t incident_id;
    uint32_t sequence;
    uint32_t session_id;
    uint64_t replay_counter;
    carelink_trigger_t trigger;
    carelink_delivery_t delivery;
    uint16_t battery_mv;
    uint16_t health_flags;
    carelink_protocol_gnss_t gnss;
    bool has_location;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t location_age_s;
    uint8_t auth_tag[CARELINK_PROTOCOL_AUTH_TAG_SIZE];
} carelink_protocol_message_t;

typedef enum {
    CARELINK_PROTOCOL_OK = 0,
    CARELINK_PROTOCOL_INVALID_ARGUMENT,
    CARELINK_PROTOCOL_BUFFER_TOO_SMALL,
    CARELINK_PROTOCOL_INVALID_SIZE,
    CARELINK_PROTOCOL_INVALID_MAGIC,
    CARELINK_PROTOCOL_UNSUPPORTED_VERSION,
    CARELINK_PROTOCOL_INVALID_LENGTH,
    CARELINK_PROTOCOL_INVALID_FIELD,
} carelink_protocol_result_t;

/* Encode writes one canonical, fixed-size, network-byte-order frame. The first
 * CARELINK_PROTOCOL_AUTHENTICATED_SIZE bytes are the authentication input; the
 * tag follows immediately. Populate auth_tag only with a cryptographic
 * component. This codec does not create or verify authentication tags. */
carelink_protocol_result_t carelink_protocol_encode(
    const carelink_protocol_message_t *message,
    uint8_t *frame,
    size_t capacity,
    size_t *encoded_size);

/* Decode validates framing and canonical field representation, then copies the
 * tag. Successful decoding does NOT authenticate the sender. A caller must
 * verify frame[0..CARELINK_PROTOCOL_AUTHENTICATED_SIZE) before acting on it. */
carelink_protocol_result_t carelink_protocol_decode(
    const uint8_t *frame,
    size_t frame_size,
    carelink_protocol_message_t *message);

carelink_protocol_result_t carelink_protocol_validate(
    const carelink_protocol_message_t *message);

#ifdef __cplusplus
}
#endif
