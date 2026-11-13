#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All times are unsigned monotonic milliseconds. Subtraction is deliberately
 * wrap-safe provided no configured interval is 2^31 ms or longer. */
typedef struct {
    uint32_t debounce_ms;
    uint32_t double_press_ms;
    uint32_t hold_ms;
    uint32_t stuck_ms;
} carelink_button_logic_config_t;

typedef enum {
    CARELINK_BUTTON_LOGIC_NONE = 0,
    CARELINK_BUTTON_LOGIC_PRESSED = 1U << 0,
    CARELINK_BUTTON_LOGIC_SOS_TRIGGERED = 1U << 1,
    CARELINK_BUTTON_LOGIC_RELEASED = 1U << 2,
    CARELINK_BUTTON_LOGIC_STUCK = 1U << 3,
    CARELINK_BUTTON_LOGIC_CALL_REQUESTED = 1U << 4,
} carelink_button_logic_flags_t;

typedef struct {
    uint32_t flags;
    uint32_t held_ms;
} carelink_button_logic_result_t;

typedef struct {
    carelink_button_logic_config_t config;
    uint32_t raw_changed_ms;
    uint32_t press_started_ms;
    uint32_t last_short_release_ms;
    bool raw_active;
    bool stable_active;
    bool sos_emitted;
    bool call_emitted;
    bool stuck_emitted;
    bool short_press_pending;
} carelink_button_logic_t;

bool carelink_button_logic_config_valid(const carelink_button_logic_config_t *config);
bool carelink_button_logic_init(carelink_button_logic_t *logic,
                                const carelink_button_logic_config_t *config,
                                bool initially_active, uint32_t now_ms);
carelink_button_logic_result_t carelink_button_logic_sample(carelink_button_logic_t *logic,
                                                            bool active, uint32_t now_ms);

/* Heltec WiFi LoRa 32 V4 header-pin policy. gps_rx_gpio is checked separately
 * so a future board configuration cannot assign the same pin to both inputs. */
bool carelink_button_pin_is_safe(int gpio, int gps_rx_gpio);

#ifdef __cplusplus
}
#endif
