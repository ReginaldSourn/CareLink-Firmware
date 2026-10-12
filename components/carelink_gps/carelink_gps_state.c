#include "carelink_gps_state.h"

carelink_gps_state_t carelink_gps_classify(const carelink_gps_snapshot_t *s,
                                        int64_t now, int64_t stale)
{
    if (s->fix.valid && s->fix_report_ms >= 0) {
        return now - s->fix_report_ms < stale ? CARELINK_GPS_FIXED : CARELINK_GPS_STALE;
    }
    if (s->last_byte_ms < 0 || now - s->last_byte_ms >= stale) return CARELINK_GPS_NO_DATA;
    if (s->last_sentence_ms < 0 || now - s->last_sentence_ms >= stale) return CARELINK_GPS_INVALID_DATA;
    return CARELINK_GPS_SEARCHING;
}

const char *carelink_gps_state_name(carelink_gps_state_t state)
{
    switch (state) {
    case CARELINK_GPS_NO_DATA: return "no UART data";
    case CARELINK_GPS_INVALID_DATA: return "no valid NMEA";
    case CARELINK_GPS_SEARCHING: return "searching";
    case CARELINK_GPS_FIXED: return "fix";
    case CARELINK_GPS_STALE: return "stale fix";
    default: return "unknown";
    }
}
