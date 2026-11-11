#include "carelink_buzzer_pattern.h"
#include "carelink_sos.h"

#include <assert.h>
#include <stdio.h>

static void test_sos_mapping(void)
{
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_IDLE, false) ==
           CARELINK_BUZZER_SILENT);
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_ARMING, false) ==
           CARELINK_BUZZER_ARMING);
    assert(carelink_buzzer_event_for_sos(
               CARELINK_SOS_ACTIVE_UNCONFIRMED, false) ==
           CARELINK_BUZZER_SOS_ACTIVE);
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_RADIO_RECEIVED, false) ==
           CARELINK_BUZZER_GATEWAY_RECEIVED);
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_CLOUD_ACCEPTED, false) ==
           CARELINK_BUZZER_CLOUD_ACCEPTED);
    assert(carelink_buzzer_event_for_sos(
               CARELINK_SOS_CAREGIVER_ACKNOWLEDGED, false) ==
           CARELINK_BUZZER_CAREGIVER_ACKNOWLEDGED);
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_RESOLVED, false) ==
           CARELINK_BUZZER_RESOLVED);
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_CANCELLED, true) ==
           CARELINK_BUZZER_CANCELLING);
    assert(carelink_buzzer_event_for_sos(CARELINK_SOS_CANCELLED, false) ==
           CARELINK_BUZZER_CANCELLED);
    assert(carelink_buzzer_event_for_sos(99, false) ==
           CARELINK_BUZZER_SILENT);
}

static void test_patterns(void)
{
    const carelink_buzzer_pattern_t silent =
        carelink_buzzer_pattern(CARELINK_BUZZER_SILENT);
    assert(silent.segments == NULL && silent.count == 0U && !silent.repeat);

    for (int value = CARELINK_BUZZER_ARMING;
         value < CARELINK_BUZZER_EVENT_COUNT; ++value) {
        const carelink_buzzer_pattern_t pattern =
            carelink_buzzer_pattern((carelink_buzzer_event_t)value);
        assert(pattern.segments != NULL && pattern.count > 0U);
        bool has_tone = false;
        for (size_t index = 0U; index < pattern.count; ++index) {
            assert(pattern.segments[index].duration_ms > 0U);
            has_tone = has_tone || pattern.segments[index].tone;
        }
        assert(has_tone);
    }
    assert(carelink_buzzer_pattern(CARELINK_BUZZER_ARMING).repeat);
    assert(carelink_buzzer_pattern(CARELINK_BUZZER_SOS_ACTIVE).repeat);
    assert(carelink_buzzer_pattern(CARELINK_BUZZER_CANCELLING).repeat);
    assert(!carelink_buzzer_pattern(
                CARELINK_BUZZER_GATEWAY_RECEIVED).repeat);
}

static void test_pin_validation(void)
{
    assert(carelink_buzzer_pin_is_safe(6, 39, 47, 48, 4));
    assert(!carelink_buzzer_pin_is_safe(4, 39, 47, 48, 4));
    assert(!carelink_buzzer_pin_is_safe(6, 6, 47, 48, 4));
    assert(!carelink_buzzer_pin_is_safe(47, 39, 47, 48, 4));
    assert(!carelink_buzzer_pin_is_safe(48, 39, 47, 48, 4));
    assert(!carelink_buzzer_pin_is_safe(7, 39, 47, 48, 4));
    assert(!carelink_buzzer_pin_is_safe(0, 39, 47, 48, 4));
}

int main(void)
{
    test_sos_mapping();
    test_patterns();
    test_pin_validation();
    puts("Buzzer SOS mapping, patterns and pin validation passed");
    return 0;
}
