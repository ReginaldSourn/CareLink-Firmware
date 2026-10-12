#pragma once

#include "carelink_nmea.h"

typedef enum {
    CARELINK_GPS_NO_DATA,
    CARELINK_GPS_INVALID_DATA,
    CARELINK_GPS_SEARCHING,
    CARELINK_GPS_FIXED,
    CARELINK_GPS_STALE,
} carelink_gps_state_t;

typedef struct {
    carelink_fix_t fix;
    carelink_gps_state_t state;
    int64_t last_byte_ms;
    int64_t last_sentence_ms;
    int64_t fix_report_ms;
    uint32_t bytes_received;
    uint32_t accepted_sentences;
    uint32_t rejected_sentences;
    uint32_t uart_errors;
} carelink_gps_snapshot_t;

/* All times are monotonic milliseconds, -1 means never received. */
carelink_gps_state_t carelink_gps_classify(const carelink_gps_snapshot_t *snapshot,
                                        int64_t now_ms, int64_t stale_ms);
const char *carelink_gps_state_name(carelink_gps_state_t state);
