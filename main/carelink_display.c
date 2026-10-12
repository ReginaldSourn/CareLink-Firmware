#include "carelink_display.h"

#include <inttypes.h>
#include <stdio.h>

#include "carelink_oled.h"
#include "esp_log.h"
#include "esp_timer.h"

static bool ready;
static const char *TAG = "display";

static void show(const char *const lines[], size_t count)
{
    if (!ready) return;
    esp_err_t err = carelink_oled_show_lines(lines, count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED update failed: %s; GPS continues over USB", esp_err_to_name(err));
        ready = false; /* Avoid flooding logs with a failed display on every loop. */
    }
}

void carelink_display_start(void)
{
    esp_err_t err = carelink_oled_init();
    ready = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "OLED unavailable: %s; GPS continues over USB", esp_err_to_name(err));
    }
    const char *lines[] = {"CARELINK GPS", "", "STARTING GNSS..."};
    show(lines, 3);
}

void carelink_display_gps_error(void)
{
    const char *lines[] = {"CARELINK GPS", "GPS START FAILED", "CHECK CONFIGURATION", "SEE USB LOG"};
    show(lines, 4);
}

void carelink_display_gps(const carelink_gps_snapshot_t *gps)
{
    if (!ready || !gps) return;
    char text[CARELINK_OLED_ROWS][CARELINK_OLED_COLUMNS + 1] = {0};
    const char *lines[CARELINK_OLED_ROWS];
    for (size_t i = 0; i < CARELINK_OLED_ROWS; ++i) lines[i] = text[i];
    snprintf(text[0], sizeof(text[0]), "CARELINK GPS");
    snprintf(text[1], sizeof(text[1]), "%s", carelink_gps_state_name(gps->state));
    if (gps->state == CARELINK_GPS_FIXED) {
        snprintf(text[2], sizeof(text[2]), "LAT %+.6f", gps->fix.latitude_deg);
        snprintf(text[3], sizeof(text[3]), "LON %+.6f", gps->fix.longitude_deg);
        if (gps->fix.has_altitude) snprintf(text[4], sizeof(text[4]), "ALT %.1f M", gps->fix.altitude_m);
        if (gps->fix.has_speed) snprintf(text[4], sizeof(text[4]), "SPEED %.2f M/S", gps->fix.speed_mps);
        if (gps->fix.has_hdop) snprintf(text[5], sizeof(text[5]), "SAT %u HDOP %.1f", gps->fix.satellites, gps->fix.hdop);
        int64_t age_ms = esp_timer_get_time() / 1000 - gps->fix_report_ms;
        snprintf(text[6], sizeof(text[6]), "AGE %.1f S", (double)age_ms / 1000.0);
    } else {
        snprintf(text[2], sizeof(text[2]), "%s", gps->state == CARELINK_GPS_NO_DATA ? "CHECK POWER / TX" : "WAITING FOR POSITION");
        snprintf(text[3], sizeof(text[3]), "%s", gps->state == CARELINK_GPS_INVALID_DATA ? "CHECK BAUD / WIRING" : "USE OPEN SKY");
        snprintf(text[4], sizeof(text[4]), "RX %" PRIu32 " B", gps->bytes_received);
        snprintf(text[5], sizeof(text[5]), "NMEA OK %" PRIu32, gps->accepted_sentences);
        snprintf(text[6], sizeof(text[6]), "NMEA BAD %" PRIu32, gps->rejected_sentences);
    }
    snprintf(text[7], sizeof(text[7]), "UART ERR %" PRIu32, gps->uart_errors);
    show(lines, CARELINK_OLED_ROWS);
}
