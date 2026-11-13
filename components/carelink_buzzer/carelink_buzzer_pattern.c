#include "carelink_buzzer_pattern.h"

#include "carelink_sos.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static const carelink_buzzer_segment_t STARTUP[] = {
    {.duration_ms = 500U, .tone = true},
};

static const carelink_buzzer_segment_t ARMING[] = {
    {.duration_ms = 100U, .tone = true},
};

static const carelink_buzzer_segment_t SOS_ACTIVE[] = {
    {.duration_ms = 180U, .tone = true},
    {.duration_ms = 120U, .tone = false},
    {.duration_ms = 180U, .tone = true},
    {.duration_ms = 120U, .tone = false},
    {.duration_ms = 180U, .tone = true},
    {.duration_ms = 1200U, .tone = false},
};

static const carelink_buzzer_segment_t CALL_REQUEST[] = {
    {.duration_ms = 180U, .tone = true},
    {.duration_ms = 120U, .tone = false},
    {.duration_ms = 180U, .tone = true},
};

static const carelink_buzzer_segment_t GATEWAY_RECEIVED[] = {
    {.duration_ms = 120U, .tone = true},
    {.duration_ms = 100U, .tone = false},
    {.duration_ms = 120U, .tone = true},
};

static const carelink_buzzer_segment_t CLOUD_ACCEPTED[] = {
    {.duration_ms = 200U, .tone = true},
    {.duration_ms = 100U, .tone = false},
    {.duration_ms = 200U, .tone = true},
};

static const carelink_buzzer_segment_t CAREGIVER_ACKNOWLEDGED[] = {
    {.duration_ms = 500U, .tone = true},
    {.duration_ms = 150U, .tone = false},
    {.duration_ms = 500U, .tone = true},
};

static const carelink_buzzer_segment_t RESOLVED[] = {
    {.duration_ms = 120U, .tone = true},
};

static const carelink_buzzer_segment_t CANCELLING[] = {
    {.duration_ms = 250U, .tone = true},
    {.duration_ms = 150U, .tone = false},
    {.duration_ms = 250U, .tone = true},
    {.duration_ms = 1000U, .tone = false},
};

static const carelink_buzzer_segment_t CANCELLED[] = {
    {.duration_ms = 100U, .tone = true},
    {.duration_ms = 100U, .tone = false},
    {.duration_ms = 100U, .tone = true},
};

carelink_buzzer_event_t carelink_buzzer_event_for_sos(
    int sos_state,
    int sos_trigger,
    bool cancellation_pending)
{
    switch ((carelink_sos_state_t)sos_state) {
    case CARELINK_SOS_ARMING:
        return CARELINK_BUZZER_ARMING;
    case CARELINK_SOS_ACTIVE_UNCONFIRMED:
        return (carelink_sos_trigger_t)sos_trigger ==
                       CARELINK_SOS_TRIGGER_CALL_REQUEST
                   ? CARELINK_BUZZER_CALL_REQUEST
                   : CARELINK_BUZZER_SOS_ACTIVE;
    case CARELINK_SOS_RADIO_RECEIVED:
        return CARELINK_BUZZER_GATEWAY_RECEIVED;
    case CARELINK_SOS_CLOUD_ACCEPTED:
        return CARELINK_BUZZER_CLOUD_ACCEPTED;
    case CARELINK_SOS_CAREGIVER_ACKNOWLEDGED:
        return CARELINK_BUZZER_CAREGIVER_ACKNOWLEDGED;
    case CARELINK_SOS_RESOLVED:
        return CARELINK_BUZZER_RESOLVED;
    case CARELINK_SOS_CANCELLED:
        return cancellation_pending ? CARELINK_BUZZER_CANCELLING
                                    : CARELINK_BUZZER_CANCELLED;
    case CARELINK_SOS_IDLE:
    default:
        return CARELINK_BUZZER_SILENT;
    }
}

carelink_buzzer_pattern_t carelink_buzzer_pattern(
    carelink_buzzer_event_t event)
{
    switch (event) {
    case CARELINK_BUZZER_STARTUP:
        return (carelink_buzzer_pattern_t){
            STARTUP, ARRAY_SIZE(STARTUP), false};
    case CARELINK_BUZZER_ARMING:
        return (carelink_buzzer_pattern_t){ARMING, ARRAY_SIZE(ARMING), false};
    case CARELINK_BUZZER_SOS_ACTIVE:
        return (carelink_buzzer_pattern_t){
            SOS_ACTIVE, ARRAY_SIZE(SOS_ACTIVE), true};
    case CARELINK_BUZZER_CALL_REQUEST:
        return (carelink_buzzer_pattern_t){
            CALL_REQUEST, ARRAY_SIZE(CALL_REQUEST), false};
    case CARELINK_BUZZER_GATEWAY_RECEIVED:
        return (carelink_buzzer_pattern_t){
            GATEWAY_RECEIVED, ARRAY_SIZE(GATEWAY_RECEIVED), false};
    case CARELINK_BUZZER_CLOUD_ACCEPTED:
        return (carelink_buzzer_pattern_t){
            CLOUD_ACCEPTED, ARRAY_SIZE(CLOUD_ACCEPTED), false};
    case CARELINK_BUZZER_CAREGIVER_ACKNOWLEDGED:
        return (carelink_buzzer_pattern_t){
            CAREGIVER_ACKNOWLEDGED, ARRAY_SIZE(CAREGIVER_ACKNOWLEDGED), false};
    case CARELINK_BUZZER_RESOLVED:
        return (carelink_buzzer_pattern_t){
            RESOLVED, ARRAY_SIZE(RESOLVED), false};
    case CARELINK_BUZZER_CANCELLING:
        return (carelink_buzzer_pattern_t){
            CANCELLING, ARRAY_SIZE(CANCELLING), true};
    case CARELINK_BUZZER_CANCELLED:
        return (carelink_buzzer_pattern_t){
            CANCELLED, ARRAY_SIZE(CANCELLED), false};
    case CARELINK_BUZZER_SILENT:
    default:
        return (carelink_buzzer_pattern_t){0};
    }
}

bool carelink_buzzer_pin_is_safe(int buzzer_gpio,
                                 int gps_gpio,
                                 int imu_sda_gpio,
                                 int imu_scl_gpio,
                                 int button_gpio)
{
    if (buzzer_gpio == gps_gpio || buzzer_gpio == imu_sda_gpio ||
        buzzer_gpio == imu_scl_gpio || buzzer_gpio == button_gpio) {
        return false;
    }
    switch (buzzer_gpio) {
    case 4:
    case 6:
    case 15:
    case 16:
    case 47:
    case 48:
        return true;
    default:
        return false;
    }
}
