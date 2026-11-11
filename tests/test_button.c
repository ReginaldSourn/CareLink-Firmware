#include "carelink_button_logic.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static const carelink_button_logic_config_t config = {
    .debounce_ms = 40,
    .hold_ms = 1000,
    .stuck_ms = 5000,
};

static carelink_button_logic_result_t sample(carelink_button_logic_t *logic,
                                             bool active, uint32_t now_ms)
{
    return carelink_button_logic_sample(logic, active, now_ms);
}

static void test_config_and_pin_validation(void)
{
    carelink_button_logic_config_t invalid = config;
    assert(carelink_button_logic_config_valid(&config));
    assert(!carelink_button_logic_config_valid(NULL));
    invalid.debounce_ms = 0;
    assert(!carelink_button_logic_config_valid(&invalid));
    invalid = config;
    invalid.hold_ms = invalid.debounce_ms;
    assert(!carelink_button_logic_config_valid(&invalid));
    invalid = config;
    invalid.stuck_ms = invalid.hold_ms;
    assert(!carelink_button_logic_config_valid(&invalid));
    invalid = config;
    invalid.stuck_ms = UINT32_C(0x80000000);
    assert(!carelink_button_logic_config_valid(&invalid));

    assert(!carelink_button_pin_is_safe(0, 39));
    assert(!carelink_button_pin_is_safe(3, 39));
    assert(!carelink_button_pin_is_safe(8, 39));
    assert(!carelink_button_pin_is_safe(14, 39));
    assert(!carelink_button_pin_is_safe(17, 39));
    assert(!carelink_button_pin_is_safe(21, 39));
    assert(!carelink_button_pin_is_safe(38, 39));
    assert(!carelink_button_pin_is_safe(39, 39));
    assert(carelink_button_pin_is_safe(4, 39));
    assert(carelink_button_pin_is_safe(48, 39));
    assert(!carelink_button_pin_is_safe(4, 4));
}

static void test_bounce(void)
{
    carelink_button_logic_t logic;
    assert(carelink_button_logic_init(&logic, &config, false, 100));
    assert(sample(&logic, true, 100).flags == 0);
    assert(sample(&logic, false, 110).flags == 0);
    assert(sample(&logic, true, 120).flags == 0);
    assert(sample(&logic, false, 130).flags == 0);
    assert(sample(&logic, true, 140).flags == 0);
    assert(sample(&logic, true, 179).flags == 0);
    carelink_button_logic_result_t result = sample(&logic, true, 180);
    assert(result.flags == CARELINK_BUTTON_LOGIC_PRESSED);
    assert(result.held_ms == 40);
}

static void test_short_press_and_release_bounce(void)
{
    carelink_button_logic_t logic;
    assert(carelink_button_logic_init(&logic, &config, false, 0));
    assert(sample(&logic, true, 0).flags == 0);
    assert(sample(&logic, true, 40).flags == CARELINK_BUTTON_LOGIC_PRESSED);
    assert(sample(&logic, false, 300).flags == 0);
    assert(sample(&logic, true, 310).flags == 0);
    assert(sample(&logic, false, 320).flags == 0);
    assert(sample(&logic, false, 359).flags == 0);
    carelink_button_logic_result_t result = sample(&logic, false, 360);
    assert(result.flags == CARELINK_BUTTON_LOGIC_RELEASED);
    assert(result.held_ms == 360);
    assert(!logic.stable_active);
}

static void test_valid_hold_fires_once(void)
{
    carelink_button_logic_t logic;
    assert(carelink_button_logic_init(&logic, &config, false, 0));
    assert(sample(&logic, true, 0).flags == 0);
    assert(sample(&logic, true, 40).flags == CARELINK_BUTTON_LOGIC_PRESSED);
    assert(sample(&logic, true, 999).flags == 0);
    carelink_button_logic_result_t result = sample(&logic, true, 1000);
    assert(result.flags == CARELINK_BUTTON_LOGIC_SOS_TRIGGERED);
    assert(result.held_ms == 1000);
    assert(sample(&logic, true, 1001).flags == 0);
    assert(sample(&logic, true, 1299).flags == 0);
    assert(sample(&logic, false, 1300).flags == 0);
    result = sample(&logic, false, 1340);
    assert(result.flags == CARELINK_BUTTON_LOGIC_RELEASED);
    assert(result.held_ms == 1340);

    /* Releasing rearms a later deliberate hold. */
    assert(sample(&logic, true, 6000).flags == 0);
    assert(sample(&logic, true, 6040).flags == CARELINK_BUTTON_LOGIC_PRESSED);
    assert(sample(&logic, true, 7000).flags == CARELINK_BUTTON_LOGIC_SOS_TRIGGERED);
}

static void test_stuck_button(void)
{
    carelink_button_logic_t logic;
    assert(carelink_button_logic_init(&logic, &config, true, 100));
    assert(sample(&logic, true, 1099).flags == 0);
    assert(sample(&logic, true, 1100).flags == CARELINK_BUTTON_LOGIC_SOS_TRIGGERED);
    assert(sample(&logic, true, 5099).flags == 0);
    carelink_button_logic_result_t result = sample(&logic, true, 5100);
    assert(result.flags == CARELINK_BUTTON_LOGIC_STUCK);
    assert(result.held_ms == 5000);
    assert(sample(&logic, true, 8000).flags == 0);
    assert(sample(&logic, false, 8100).flags == 0);
    result = sample(&logic, false, 8140);
    assert(result.flags == CARELINK_BUTTON_LOGIC_RELEASED);
}

static void test_timestamp_wrap(void)
{
    carelink_button_logic_t logic;
    const uint32_t start = UINT32_MAX - 20U;
    assert(carelink_button_logic_init(&logic, &config, false, start));
    assert(sample(&logic, true, UINT32_MAX - 10U).flags == 0);
    assert(sample(&logic, true, 28U).flags == 0); /* 39 ms across wrap. */
    assert(sample(&logic, true, 29U).flags == CARELINK_BUTTON_LOGIC_PRESSED);
    assert(sample(&logic, true, 988U).flags == 0);
    carelink_button_logic_result_t result = sample(&logic, true, 989U);
    assert(result.flags == CARELINK_BUTTON_LOGIC_SOS_TRIGGERED);
    assert(result.held_ms == 1000U);
}

int main(void)
{
    test_config_and_pin_validation();
    test_bounce();
    test_short_press_and_release_bounce();
    test_valid_hold_fires_once();
    test_stuck_button();
    test_timestamp_wrap();
    puts("SOS button state, safety and timestamp-wrap tests passed");
    return 0;
}
