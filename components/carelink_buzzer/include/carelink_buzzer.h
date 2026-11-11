#pragma once

#include <stdbool.h>

#include "carelink_sos.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts a non-blocking PWM feedback task. The configured GPIO must drive a
 * transistor; it must not source buzzer current directly. */
esp_err_t carelink_buzzer_start(void);

/* Safe to call from a task callback. Duplicate states are ignored so location
 * updates and retries do not replay a completed acknowledgement pattern. */
esp_err_t carelink_buzzer_notify_sos(carelink_sos_state_t state,
                                     bool cancellation_pending);

#ifdef __cplusplus
}
#endif
