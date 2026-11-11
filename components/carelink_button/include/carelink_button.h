#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARELINK_BUTTON_EVENT_PRESSED,
    CARELINK_BUTTON_EVENT_SOS_TRIGGERED,
    CARELINK_BUTTON_EVENT_RELEASED,
    CARELINK_BUTTON_EVENT_STUCK,
} carelink_button_event_type_t;

typedef struct {
    carelink_button_event_type_t type;
    uint32_t timestamp_ms;
    uint32_t held_ms;
    uint32_t sequence;
    uint32_t dropped_events;
} carelink_button_event_t;

typedef void (*carelink_button_event_callback_t)(const carelink_button_event_t *event,
                                                 void *context);

typedef struct {
    uint32_t published_events;
    uint32_t dropped_events;
    uint32_t last_edge_timestamp_ms;
    bool pressed;
    bool sos_triggered;
    bool stuck;
} carelink_button_stats_t;

/* The component is disabled by default until the production SOS GPIO is
 * confirmed. When disabled, start returns ESP_ERR_NOT_SUPPORTED. */
esp_err_t carelink_button_start(void);

/* Callbacks run in the button task and must not block. Passing NULL removes
 * the callback. The incident manager should use the callback; events are also
 * retained in a bounded drop-oldest queue for polling/diagnostics. Sequence
 * gaps and dropped_events make any queue overrun observable. UINT32_MAX waits
 * indefinitely. */
esp_err_t carelink_button_set_callback(carelink_button_event_callback_t callback,
                                       void *context);
esp_err_t carelink_button_receive(carelink_button_event_t *event, uint32_t timeout_ms);
esp_err_t carelink_button_get_stats(carelink_button_stats_t *stats);

#ifdef __cplusplus
}
#endif
