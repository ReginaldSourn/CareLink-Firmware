#include "carelink_display.h"

#include <stddef.h>

#include "carelink_display_format.h"
#include "carelink_oled.h"
#include "esp_log.h"

static bool ready;
static const char *TAG = "display";

_Static_assert(CARELINK_DISPLAY_ROWS == CARELINK_OLED_ROWS,
               "display formatter and OLED row counts must match");
_Static_assert(CARELINK_DISPLAY_COLUMNS == CARELINK_OLED_COLUMNS,
               "display formatter and OLED column counts must match");

static void show(const char *const lines[], size_t count)
{
    if (!ready) {
        return;
    }
    const esp_err_t error = carelink_oled_show_lines(lines, count);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "OLED update failed: %s; status continues over USB",
                 esp_err_to_name(error));
        ready = false; /* Avoid flooding logs after a display failure. */
    }
}

static void show_view(const carelink_display_view_t *view)
{
    char text[CARELINK_DISPLAY_ROWS][CARELINK_DISPLAY_COLUMNS + 1U];
    const char *lines[CARELINK_DISPLAY_ROWS];
    carelink_display_format(view, text);
    for (size_t row = 0; row < CARELINK_DISPLAY_ROWS; ++row) {
        lines[row] = text[row];
    }
    show(lines, CARELINK_DISPLAY_ROWS);
}

static carelink_display_gps_t gps_state(
    const carelink_display_status_t *status)
{
    if (!status->gps_available) {
        return CARELINK_DISPLAY_GPS_OFFLINE;
    }
    switch (status->gps.state) {
    case CARELINK_GPS_FIXED:
        return CARELINK_DISPLAY_GPS_READY;
    case CARELINK_GPS_SEARCHING:
        return CARELINK_DISPLAY_GPS_SEARCHING;
    case CARELINK_GPS_STALE:
        return CARELINK_DISPLAY_GPS_STALE;
    case CARELINK_GPS_NO_DATA:
    case CARELINK_GPS_INVALID_DATA:
    default:
        return CARELINK_DISPLAY_GPS_CHECK;
    }
}

static carelink_display_sos_t sos_state(
    const carelink_display_status_t *status)
{
    if (!status->incident_available || !status->incident.running) {
        return CARELINK_DISPLAY_SOS_UNAVAILABLE;
    }
    const bool call_request =
        status->incident.trigger == CARELINK_SOS_TRIGGER_CALL_REQUEST;
    switch (status->incident.state) {
    case CARELINK_SOS_IDLE:
        return CARELINK_DISPLAY_SOS_READY;
    case CARELINK_SOS_ARMING:
        return CARELINK_DISPLAY_SOS_ARMING;
    case CARELINK_SOS_ACTIVE_UNCONFIRMED:
        return call_request ? CARELINK_DISPLAY_CALL_SENDING
                            : CARELINK_DISPLAY_SOS_SENDING;
    case CARELINK_SOS_RADIO_RECEIVED:
        return call_request ? CARELINK_DISPLAY_CALL_GATEWAY_RECEIVED
                            : CARELINK_DISPLAY_SOS_GATEWAY_RECEIVED;
    case CARELINK_SOS_CLOUD_ACCEPTED:
        return call_request ? CARELINK_DISPLAY_CALL_CLOUD_ACCEPTED
                            : CARELINK_DISPLAY_SOS_CLOUD_ACCEPTED;
    case CARELINK_SOS_CAREGIVER_ACKNOWLEDGED:
        return call_request
                   ? CARELINK_DISPLAY_CALL_CAREGIVER_ACKNOWLEDGED
                   : CARELINK_DISPLAY_SOS_CAREGIVER_ACKNOWLEDGED;
    case CARELINK_SOS_RESOLVED:
        return CARELINK_DISPLAY_SOS_RESOLVED;
    case CARELINK_SOS_CANCELLED:
        return status->incident.configuration_locked
                   ? CARELINK_DISPLAY_SOS_CANCELLING
                   : CARELINK_DISPLAY_SOS_CANCELLED;
    default:
        return CARELINK_DISPLAY_SOS_UNAVAILABLE;
    }
}

void carelink_display_start(void)
{
    const esp_err_t error = carelink_oled_init();
    ready = error == ESP_OK;
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "OLED unavailable: %s; status continues over USB",
                 esp_err_to_name(error));
    }
    show_view(NULL);
}

void carelink_display_update(const carelink_display_status_t *status)
{
    if (!ready || status == NULL) {
        return;
    }
    const carelink_display_view_t view = {
        .sos = sos_state(status),
        .gps = gps_state(status),
        .battery_available = status->battery_available,
        .battery_mv = status->battery_mv,
    };
    show_view(&view);
}
