#include "carelink_nmea.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Reject exponents, NaN, infinity, whitespace and partially parsed fields. */
static bool decimal(const char *s, double *value)
{
    const char *p = s;
    if (*p == '-') ++p;
    bool digit = false, dot = false;
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') digit = true;
        else if (*p == '.' && !dot) dot = true;
        else return false;
    }
    if (!digit) return false;
    *value = strtod(s, NULL);
    return isfinite(*value);
}

static bool integer(const char *s, unsigned max, unsigned *value)
{
    unsigned n = 0;
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        n = n * 10 + (unsigned)(*s - '0');
        if (n > max) return false;
    }
    *value = n;
    return true;
}

static bool coordinate(const char *s, const char *hemisphere, bool latitude,
                       double *value)
{
    size_t digits = latitude ? 4 : 5;
    if (strlen(s) < digits || (s[digits] && s[digits] != '.')) return false;
    for (size_t i = 0; i < digits; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    if (strlen(hemisphere) != 1) return false;
    char h = hemisphere[0];
    if (latitude ? (h != 'N' && h != 'S') : (h != 'E' && h != 'W')) return false;
    double raw;
    if (!decimal(s, &raw) || raw < 0) return false;
    double degrees = floor(raw / 100.0);
    double minutes = raw - degrees * 100.0;
    double limit = latitude ? 90.0 : 180.0;
    if (minutes >= 60 || degrees > limit || (degrees == limit && minutes != 0)) return false;
    *value = (degrees + minutes / 60.0) * ((h == 'S' || h == 'W') ? -1 : 1);
    return true;
}

static bool utc_valid(const char *s)
{
    if (strlen(s) < 6 || (s[6] && s[6] != '.')) return false;
    for (size_t i = 0; i < 6; ++i) if (s[i] < '0' || s[i] > '9') return false;
    double utc;
    return decimal(s, &utc) && utc < 240000 &&
           (s[2] - '0') * 10 + s[3] - '0' < 60 &&
           (s[4] - '0') * 10 + s[5] - '0' <= 60;
}

static carelink_nmea_result_t parse(char *line, carelink_fix_t *out)
{
    char *star = strchr(line, '*');
    if (!star || strlen(star + 1) != 2) return CARELINK_NMEA_REJECTED;
    int hi = hex_digit(star[1]), lo = hex_digit(star[2]);
    unsigned checksum = 0;
    for (char *p = line + 1; p < star; ++p) checksum ^= (unsigned char)*p;
    if (hi < 0 || lo < 0 || checksum != (unsigned)(hi * 16 + lo)) return CARELINK_NMEA_REJECTED;
    *star = '\0';
    char *fields[24];
    size_t count = 1;
    fields[0] = line + 1;
    for (char *p = line + 1; *p; ++p) {
        if (*p == ',') {
            if (count == 24) return CARELINK_NMEA_REJECTED;
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    if (strlen(fields[0]) != 5 || fields[0][0] < 'A' || fields[0][0] > 'Z' ||
        fields[0][1] < 'A' || fields[0][1] > 'Z') return CARELINK_NMEA_REJECTED;
    bool gga = strcmp(fields[0] + 2, "GGA") == 0;
    bool rmc = strcmp(fields[0] + 2, "RMC") == 0;
    if (!gga && !rmc) return CARELINK_NMEA_OTHER;
    if (count < (gga ? 15U : 12U)) return CARELINK_NMEA_REJECTED;
    carelink_fix_t fix = {0};
    memcpy(fix.sentence_type, gga ? "GGA" : "RMC", 4);
    if (gga) {
        if (!integer(fields[6], 8, &fix.quality)) return CARELINK_NMEA_REJECTED;
        /* Estimated/manual/simulated positions are not live GNSS fixes. */
        fix.valid = fix.quality >= 1 && fix.quality <= 5;
    } else {
        if (strcmp(fields[2], "A") && strcmp(fields[2], "V")) return CARELINK_NMEA_REJECTED;
        fix.valid = !strcmp(fields[2], "A");
        if (count > 12 && *fields[12] &&
            strcmp(fields[12], "A") && strcmp(fields[12], "D") &&
            strcmp(fields[12], "R") && strcmp(fields[12], "F")) fix.valid = false;
    }
    if (fix.valid) {
        size_t lat = gga ? 2 : 3;
        if (!utc_valid(fields[1]) ||
            !coordinate(fields[lat], fields[lat + 1], true, &fix.latitude_deg) ||
            !coordinate(fields[lat + 2], fields[lat + 3], false, &fix.longitude_deg))
            return CARELINK_NMEA_REJECTED;
        if (gga) {
            integer(fields[7], 99, &fix.satellites);
            fix.has_hdop = decimal(fields[8], &fix.hdop) && fix.hdop >= 0;
            fix.has_altitude = !strcmp(fields[10], "M") && decimal(fields[9], &fix.altitude_m);
        } else {
            fix.has_speed = decimal(fields[7], &fix.speed_mps) && fix.speed_mps >= 0;
            if (fix.has_speed) fix.speed_mps *= 0.514444444444;
        }
    }
    *out = fix;
    return CARELINK_NMEA_FIX;
}

carelink_nmea_result_t carelink_nmea_feed(carelink_nmea_t *p, uint8_t byte,
                                        carelink_fix_t *fix)
{
    if (!p || !fix) return CARELINK_NMEA_REJECTED;
    if (byte == '$') {
        p->length = 1;
        p->line[0] = '$';
        p->collecting = true;
        return CARELINK_NMEA_NONE;
    }
    if (!p->collecting) return CARELINK_NMEA_NONE;
    if (byte == '\r' || byte == '\n') {
        p->collecting = false;
        p->line[p->length] = '\0';
        return parse(p->line, fix);
    }
    if (byte < 32 || byte > 126 || p->length >= sizeof(p->line) - 1) {
        p->collecting = false;
        return CARELINK_NMEA_REJECTED;
    }
    p->line[p->length++] = (char)byte;
    return CARELINK_NMEA_NONE;
}
