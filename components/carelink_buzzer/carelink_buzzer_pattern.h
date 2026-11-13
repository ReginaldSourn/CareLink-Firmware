#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CARELINK_BUZZER_SILENT = 0,
    CARELINK_BUZZER_STARTUP,
    CARELINK_BUZZER_ARMING,
    CARELINK_BUZZER_SOS_ACTIVE,
    CARELINK_BUZZER_CALL_REQUEST,
    CARELINK_BUZZER_GATEWAY_RECEIVED,
    CARELINK_BUZZER_CLOUD_ACCEPTED,
    CARELINK_BUZZER_CAREGIVER_ACKNOWLEDGED,
    CARELINK_BUZZER_RESOLVED,
    CARELINK_BUZZER_CANCELLING,
    CARELINK_BUZZER_CANCELLED,
    CARELINK_BUZZER_EVENT_COUNT,
} carelink_buzzer_event_t;

typedef struct {
    uint16_t duration_ms;
    bool tone;
} carelink_buzzer_segment_t;

typedef struct {
    const carelink_buzzer_segment_t *segments;
    size_t count;
    bool repeat;
} carelink_buzzer_pattern_t;

carelink_buzzer_event_t carelink_buzzer_event_for_sos(
    int sos_state,
    int sos_trigger,
    bool cancellation_pending);

carelink_buzzer_pattern_t carelink_buzzer_pattern(
    carelink_buzzer_event_t event);

bool carelink_buzzer_pin_is_safe(int buzzer_gpio,
                                 int gps_gpio,
                                 int imu_sda_gpio,
                                 int imu_scl_gpio,
                                 int button_gpio);
