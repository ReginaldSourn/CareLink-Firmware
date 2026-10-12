#include "carelink_gps_state.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static carelink_nmea_result_t feed(carelink_nmea_t *p, carelink_fix_t *fix, const char *s)
{
    carelink_nmea_result_t result = CARELINK_NMEA_NONE;
    for (; *s; ++s) {
        carelink_nmea_result_t next = carelink_nmea_feed(p, (uint8_t)*s, fix);
        if (next != CARELINK_NMEA_NONE) result = next;
    }
    return result;
}

static carelink_nmea_result_t sentence(carelink_nmea_t *p, carelink_fix_t *fix, const char *body)
{
    unsigned sum = 0;
    for (const char *s = body; *s; ++s) sum ^= (unsigned char)*s;
    char line[256];
    snprintf(line, sizeof(line), "$%s*%02X\r\n", body, sum);
    return feed(p, fix, line);
}

static void near(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.000001);
}

static void test_navigation(void)
{
    carelink_nmea_t p = {0};
    carelink_fix_t fix = {0};
    /* Independent published-style checksum fixture, split as UART chunks. */
    assert(feed(&p, &fix, "noise\r\n$GPGGA,123519,4807.038,N,01131.") == CARELINK_NMEA_NONE);
    assert(feed(&p, &fix, "000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n") == CARELINK_NMEA_FIX);
    assert(fix.valid && fix.has_altitude && fix.has_hdop && !fix.has_speed);
    near(fix.latitude_deg, 48.1173);
    near(fix.longitude_deg, 11.5166666667);
    near(fix.altitude_m, 545.4);
    assert(fix.satellites == 8 && fix.quality == 1);

    assert(feed(&p, &fix, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n") == CARELINK_NMEA_FIX);
    assert(fix.valid && fix.has_speed && !fix.has_altitude && !fix.has_hdop);
    near(fix.speed_mps, 11.5235555556);

    assert(sentence(&p, &fix, "GNRMC,010203.00,A,1230.000,S,10430.000,W,0.0,,240926,,,A") == CARELINK_NMEA_FIX);
    near(fix.latitude_deg, -12.5);
    near(fix.longitude_deg, -104.5);
    assert(sentence(&p, &fix, "BDGGA,010203,0000.000,N,00000.000,E,1,10,1.2,-5.0,M,0,M,,") == CARELINK_NMEA_FIX);
    assert(fix.valid);
    near(fix.latitude_deg, 0);
    near(fix.longitude_deg, 0);
    near(fix.altitude_m, -5);

    assert(sentence(&p, &fix, "GNGGA,,,,,,0,00,99.99,,,,,,") == CARELINK_NMEA_FIX);
    assert(!fix.valid && !fix.has_altitude);
    assert(sentence(&p, &fix, "GNRMC,,V,,,,,,,,,,N") == CARELINK_NMEA_FIX);
    assert(!fix.valid);
    assert(sentence(&p, &fix, "GNGGA,010203,9000.000,N,18000.000,E,1,10,1,0,M,0,M,,") == CARELINK_NMEA_FIX);
    near(fix.latitude_deg, 90);
    near(fix.longitude_deg, 180);

    const char *simulated[] = {
        "GNRMC,010203,A,1230.000,N,10430.000,E,1,,240926,,,N",
        "GNRMC,010203,A,1230.000,N,10430.000,E,1,,240926,,,E",
        "GNGGA,010203,1230.000,N,10430.000,E,6,10,1,0,M,0,M,,",
        "GNGGA,010203,1230.000,N,10430.000,E,8,10,1,0,M,0,M,,",
    };
    for (size_t i = 0; i < sizeof(simulated) / sizeof(simulated[0]); ++i) {
        assert(sentence(&p, &fix, simulated[i]) == CARELINK_NMEA_FIX);
        assert(!fix.valid);
    }
}

static void test_corruption(void)
{
    carelink_nmea_t p = {0};
    carelink_fix_t fix = {.latitude_deg = 55};
    assert(feed(&p, &fix, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00\n") == CARELINK_NMEA_REJECTED);
    near(fix.latitude_deg, 55);
    assert(feed(&p, &fix, "$GPGGA,123519\n") == CARELINK_NMEA_REJECTED);
    assert(feed(&p, &fix, "$GPRMC*ZZ\n") == CARELINK_NMEA_REJECTED);
    assert(feed(&p, &fix, "$GPRMC*00junk\n") == CARELINK_NMEA_REJECTED);

    const char *bad[] = {
        "GNGGA,010203,1260.000,N,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,010203,9000.001,N,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,010203,1230.000,N,18000.001,E,1,10,1,0,M,0,M,,",
        "GNGGA,010203,NaN,N,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,010203,12e3,N,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,010203,1230.000,X,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,240000,1230.000,N,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,126100,1230.000,N,10430.000,E,1,10,1,0,M,0,M,,",
        "GNGGA,010203,1230.000,N,10430.000,E,1x,10,1,0,M,0,M,,",
        "GNRMC,010203,A,,,,,0,,240926,,,A",
        "GNRMC,010203,Q,1230.000,N,10430.000,E,0,,240926,,,A",
        "GNGGA,0,0",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(sentence(&p, &fix, bad[i]) == CARELINK_NMEA_REJECTED);
        near(fix.latitude_deg, 55);
    }
    assert(sentence(&p, &fix, "GPGSV,1,1,00") == CARELINK_NMEA_OTHER);
    near(fix.latitude_deg, 55);

    feed(&p, &fix, "$discarded");
    assert(sentence(&p, &fix, "GNRMC,,V,,,,,,,,,,N") == CARELINK_NMEA_FIX);
    feed(&p, &fix, "$");
    for (int i = 0; i < 200; ++i) carelink_nmea_feed(&p, 'x', &fix);
    assert(!p.collecting);
    assert(sentence(&p, &fix, "GNRMC,,V,,,,,,,,,,N") == CARELINK_NMEA_FIX);
    feed(&p, &fix, "$GNRMC,");
    assert(carelink_nmea_feed(&p, 0, &fix) == CARELINK_NMEA_REJECTED);
    assert(sentence(&p, &fix, "GNRMC,,V,,,,,,,,,,N") == CARELINK_NMEA_FIX);

    /* Deterministic arbitrary-byte stress under sanitizers, then recovery. */
    uint32_t random = 7;
    for (int i = 0; i < 100000; ++i) {
        random = random * 1664525U + 1013904223U;
        carelink_nmea_feed(&p, (uint8_t)(random >> 24), &fix);
    }
    assert(sentence(&p, &fix, "GNRMC,,V,,,,,,,,,,N") == CARELINK_NMEA_FIX);
}

static void test_freshness(void)
{
    carelink_gps_snapshot_t s = {.last_byte_ms = -1, .last_sentence_ms = -1, .fix_report_ms = -1};
    assert(carelink_gps_classify(&s, 1000, 5000) == CARELINK_GPS_NO_DATA);
    s.last_byte_ms = 1000;
    assert(carelink_gps_classify(&s, 1000, 5000) == CARELINK_GPS_INVALID_DATA);
    s.last_sentence_ms = 1000;
    assert(carelink_gps_classify(&s, 1000, 5000) == CARELINK_GPS_SEARCHING);
    s.fix.valid = true;
    s.fix_report_ms = 1000;
    assert(carelink_gps_classify(&s, 5999, 5000) == CARELINK_GPS_FIXED);
    assert(carelink_gps_classify(&s, 6000, 5000) == CARELINK_GPS_STALE);
    s.last_byte_ms = s.last_sentence_ms = 6000; /* GSV must not refresh a position. */
    assert(carelink_gps_classify(&s, 6000, 5000) == CARELINK_GPS_STALE);
    s.fix.valid = false; /* Explicit V/quality 0 invalidates immediately. */
    assert(carelink_gps_classify(&s, 6000, 5000) == CARELINK_GPS_SEARCHING);
    assert(carelink_gps_classify(&s, 11000, 5000) == CARELINK_GPS_NO_DATA);
    s.fix.valid = true;
    s.fix_report_ms = INT64_C(5000000000);
    assert(carelink_gps_classify(&s, INT64_C(5000000001), 5000) == CARELINK_GPS_FIXED);
}

int main(void)
{
    test_navigation();
    test_corruption();
    test_freshness();
    puts("GPS parser, corruption recovery and freshness tests passed");
    return 0;
}
