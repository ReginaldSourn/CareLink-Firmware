#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_DISPLAY_ROWS 8U
#define CARELINK_DISPLAY_COLUMNS 21U

typedef enum {
    CARELINK_DISPLAY_SOS_UNAVAILABLE = 0,
    CARELINK_DISPLAY_SOS_READY,
    CARELINK_DISPLAY_SOS_ARMING,
    CARELINK_DISPLAY_SOS_SENDING,
    CARELINK_DISPLAY_SOS_GATEWAY_RECEIVED,
    CARELINK_DISPLAY_SOS_CLOUD_ACCEPTED,
    CARELINK_DISPLAY_SOS_CAREGIVER_ACKNOWLEDGED,
    CARELINK_DISPLAY_SOS_RESOLVED,
    CARELINK_DISPLAY_SOS_CANCELLING,
    CARELINK_DISPLAY_SOS_CANCELLED,
    CARELINK_DISPLAY_CALL_SENDING,
    CARELINK_DISPLAY_CALL_GATEWAY_RECEIVED,
    CARELINK_DISPLAY_CALL_CLOUD_ACCEPTED,
    CARELINK_DISPLAY_CALL_CAREGIVER_ACKNOWLEDGED,
} carelink_display_sos_t;

typedef enum {
    CARELINK_DISPLAY_GPS_OFFLINE = 0,
    CARELINK_DISPLAY_GPS_SEARCHING,
    CARELINK_DISPLAY_GPS_READY,
    CARELINK_DISPLAY_GPS_STALE,
    CARELINK_DISPLAY_GPS_CHECK,
} carelink_display_gps_t;

typedef struct {
    carelink_display_sos_t sos;
    carelink_display_gps_t gps;
    bool battery_available;
    uint32_t battery_mv;
} carelink_display_view_t;

void carelink_display_format(
    const carelink_display_view_t *view,
    char lines[CARELINK_DISPLAY_ROWS][CARELINK_DISPLAY_COLUMNS + 1U]);

#ifdef __cplusplus
}
#endif
