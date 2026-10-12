#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One sentence, never a mixture of fields from different navigation epochs. */
typedef struct {
    bool valid;
    double latitude_deg;
    double longitude_deg;
    bool has_altitude;
    double altitude_m;
    bool has_speed;
    double speed_mps;
    bool has_hdop;
    double hdop;
    unsigned satellites;
    unsigned quality;
    char sentence_type[4];
} carelink_fix_t;

typedef enum {
    CARELINK_NMEA_NONE,
    CARELINK_NMEA_FIX,
    CARELINK_NMEA_OTHER,
    CARELINK_NMEA_REJECTED,
} carelink_nmea_result_t;

typedef struct {
    char line[128];
    size_t length;
    bool collecting;
} carelink_nmea_t;

/* Zero-initialize before use. No allocation; feed any UART chunk byte by byte.
 * FIX includes explicit no-fix reports (fix.valid == false). Other outcomes
 * leave the caller's fix unchanged. Checksum is mandatory. */
carelink_nmea_result_t carelink_nmea_feed(carelink_nmea_t *parser,
                                        uint8_t byte, carelink_fix_t *fix);
