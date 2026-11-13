#include "carelink_button_logic.h"

#include <limits.h>
#include <stddef.h>

static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

bool carelink_button_logic_config_valid(const carelink_button_logic_config_t *config)
{
    return config != NULL && config->debounce_ms > 0U &&
           config->double_press_ms > config->debounce_ms &&
           config->double_press_ms < config->hold_ms &&
           config->hold_ms > config->debounce_ms &&
           config->stuck_ms > config->hold_ms &&
           config->debounce_ms <= INT32_MAX &&
           config->double_press_ms <= INT32_MAX &&
           config->hold_ms <= INT32_MAX &&
           config->stuck_ms <= INT32_MAX;
}

bool carelink_button_logic_init(carelink_button_logic_t *logic,
                                const carelink_button_logic_config_t *config,
                                bool initially_active, uint32_t now_ms)
{
    if (logic == NULL || !carelink_button_logic_config_valid(config)) return false;
    *logic = (carelink_button_logic_t) {
        .config = *config,
        .raw_changed_ms = now_ms,
        .press_started_ms = now_ms,
        .raw_active = initially_active,
        .stable_active = initially_active,
    };
    return true;
}

carelink_button_logic_result_t carelink_button_logic_sample(carelink_button_logic_t *logic,
                                                            bool active, uint32_t now_ms)
{
    carelink_button_logic_result_t result = {0};
    if (logic == NULL) return result;

    if (logic->short_press_pending &&
        elapsed_ms(now_ms, logic->last_short_release_ms) >
            logic->config.double_press_ms) {
        logic->short_press_pending = false;
    }

    if (active != logic->raw_active) {
        logic->raw_active = active;
        logic->raw_changed_ms = now_ms;
    }

    if (logic->raw_active != logic->stable_active &&
        elapsed_ms(now_ms, logic->raw_changed_ms) >= logic->config.debounce_ms) {
        logic->stable_active = logic->raw_active;
        if (logic->stable_active) {
            /* Use the beginning of the stable interval, not the later polling
             * instant, so scheduling jitter does not extend the required hold. */
            logic->press_started_ms = logic->raw_changed_ms;
            logic->sos_emitted = false;
            logic->call_emitted = false;
            logic->stuck_emitted = false;
            result.flags |= CARELINK_BUTTON_LOGIC_PRESSED;
            if (logic->short_press_pending) {
                logic->short_press_pending = false;
                logic->call_emitted = true;
                result.flags |= CARELINK_BUTTON_LOGIC_CALL_REQUESTED;
            }
        } else {
            result.held_ms = elapsed_ms(now_ms, logic->press_started_ms);
            result.flags |= CARELINK_BUTTON_LOGIC_RELEASED;
            if (!logic->sos_emitted && !logic->call_emitted) {
                logic->last_short_release_ms = now_ms;
                logic->short_press_pending = true;
            }
        }
    }

    if (logic->stable_active) {
        result.held_ms = elapsed_ms(now_ms, logic->press_started_ms);
        if (!logic->sos_emitted && result.held_ms >= logic->config.hold_ms) {
            logic->sos_emitted = true;
            logic->short_press_pending = false;
            result.flags |= CARELINK_BUTTON_LOGIC_SOS_TRIGGERED;
        }
        if (!logic->stuck_emitted && result.held_ms >= logic->config.stuck_ms) {
            logic->stuck_emitted = true;
            result.flags |= CARELINK_BUTTON_LOGIC_STUCK;
        }
    }
    return result;
}

bool carelink_button_pin_is_safe(int gpio, int gps_rx_gpio)
{
    if (gpio == gps_rx_gpio) return false;
    /* These are the free header candidates common to the supported V4 R2/R8
     * profiles after excluding GNSS GPIO38/39. An allow-list also rejects the
     * ESP32-S3 strapping pins (including GPIO0), onboard radio/FEM GPIO2/5/7-14,
     * OLED GPIO17/18/21, USB, flash/PSRAM, battery and power-control signals. */
    switch (gpio) {
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
