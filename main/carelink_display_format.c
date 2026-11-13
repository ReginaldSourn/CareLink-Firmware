#include "carelink_display_format.h"

#include <stdio.h>
#include <string.h>

static void copy_line(char output[CARELINK_DISPLAY_COLUMNS + 1U],
                      const char *text)
{
    snprintf(output, CARELINK_DISPLAY_COLUMNS + 1U, "%s", text);
}

static void format_sos(
    carelink_display_sos_t state,
    char lines[CARELINK_DISPLAY_ROWS][CARELINK_DISPLAY_COLUMNS + 1U])
{
    switch (state) {
    case CARELINK_DISPLAY_SOS_READY:
        copy_line(lines[1], "STATUS READY");
        copy_line(lines[2], "SOS READY");
        copy_line(lines[7], "HOLD SOS FOR HELP");
        break;
    case CARELINK_DISPLAY_SOS_ARMING:
        copy_line(lines[1], "SOS ARMING");
        copy_line(lines[2], "KEEP HOLDING BUTTON");
        copy_line(lines[7], "RELEASE TO CANCEL");
        break;
    case CARELINK_DISPLAY_SOS_SENDING:
        copy_line(lines[1], "SOS ACTIVE");
        copy_line(lines[2], "CONTACTING GATEWAY");
        copy_line(lines[7], "DO NOT POWER OFF");
        break;
    case CARELINK_DISPLAY_SOS_GATEWAY_RECEIVED:
        copy_line(lines[1], "SOS SENT");
        copy_line(lines[2], "GATEWAY RECEIVED");
        copy_line(lines[7], "WAIT FOR CONFIRMATION");
        break;
    case CARELINK_DISPLAY_SOS_CLOUD_ACCEPTED:
        copy_line(lines[1], "SOS RECEIVED");
        copy_line(lines[2], "CLOUD CONFIRMED");
        copy_line(lines[7], "WAIT FOR CAREGIVER");
        break;
    case CARELINK_DISPLAY_SOS_CAREGIVER_ACKNOWLEDGED:
        copy_line(lines[1], "HELP CONFIRMED");
        copy_line(lines[2], "CAREGIVER ACK");
        copy_line(lines[7], "HELP IS RESPONDING");
        break;
    case CARELINK_DISPLAY_SOS_RESOLVED:
        copy_line(lines[1], "SOS RESOLVED");
        copy_line(lines[2], "INCIDENT CLOSED");
        copy_line(lines[7], "HOLD SOS FOR HELP");
        break;
    case CARELINK_DISPLAY_SOS_CANCELLING:
        copy_line(lines[1], "SOS CANCELLED");
        copy_line(lines[2], "SENDING CANCELLATION");
        copy_line(lines[7], "DO NOT POWER OFF");
        break;
    case CARELINK_DISPLAY_SOS_CANCELLED:
        copy_line(lines[1], "SOS CANCELLED");
        copy_line(lines[2], "CANCELLATION SENT");
        copy_line(lines[7], "HOLD SOS FOR HELP");
        break;
    case CARELINK_DISPLAY_CALL_SENDING:
        copy_line(lines[1], "CALL REQUEST");
        copy_line(lines[2], "CONTACTING GATEWAY");
        copy_line(lines[7], "DO NOT POWER OFF");
        break;
    case CARELINK_DISPLAY_CALL_GATEWAY_RECEIVED:
        copy_line(lines[1], "CALL REQUEST SENT");
        copy_line(lines[2], "GATEWAY RECEIVED");
        copy_line(lines[7], "WAIT FOR CAREGIVER");
        break;
    case CARELINK_DISPLAY_CALL_CLOUD_ACCEPTED:
        copy_line(lines[1], "CALL REQUEST SENT");
        copy_line(lines[2], "CLOUD CONFIRMED");
        copy_line(lines[7], "WAIT FOR CAREGIVER");
        break;
    case CARELINK_DISPLAY_CALL_CAREGIVER_ACKNOWLEDGED:
        copy_line(lines[1], "CALL CONFIRMED");
        copy_line(lines[2], "CAREGIVER ACK");
        copy_line(lines[7], "CAREGIVER RESPONDING");
        break;
    case CARELINK_DISPLAY_SOS_UNAVAILABLE:
    default:
        copy_line(lines[1], "SOS NOT READY");
        copy_line(lines[2], "SERVICE CHECK NEEDED");
        copy_line(lines[7], "SEE USB LOG");
        break;
    }
}

static const char *gps_text(carelink_display_gps_t state)
{
    switch (state) {
    case CARELINK_DISPLAY_GPS_READY:
        return "GPS READY";
    case CARELINK_DISPLAY_GPS_SEARCHING:
        return "GPS SEARCHING";
    case CARELINK_DISPLAY_GPS_STALE:
        return "GPS STALE";
    case CARELINK_DISPLAY_GPS_CHECK:
        return "GPS CHECK";
    case CARELINK_DISPLAY_GPS_OFFLINE:
    default:
        return "GPS OFFLINE";
    }
}

void carelink_display_format(
    const carelink_display_view_t *view,
    char lines[CARELINK_DISPLAY_ROWS][CARELINK_DISPLAY_COLUMNS + 1U])
{
    if (lines == NULL) {
        return;
    }
    memset(lines, 0,
           CARELINK_DISPLAY_ROWS * (CARELINK_DISPLAY_COLUMNS + 1U));
    copy_line(lines[0], "CARELINK");
    if (view == NULL) {
        copy_line(lines[1], "STARTING");
        copy_line(lines[7], "PLEASE WAIT");
        return;
    }

    format_sos(view->sos, lines);
    copy_line(lines[4], gps_text(view->gps));
    if (view->battery_available) {
        const unsigned long volts =
            (unsigned long)(view->battery_mv / 1000U);
        const unsigned long hundredths =
            (unsigned long)((view->battery_mv % 1000U) / 10U);
        snprintf(lines[5], CARELINK_DISPLAY_COLUMNS + 1U,
                 "BATTERY %lu.%02lu V", volts, hundredths);
    } else {
        copy_line(lines[5], "BATTERY N/A");
    }
}
