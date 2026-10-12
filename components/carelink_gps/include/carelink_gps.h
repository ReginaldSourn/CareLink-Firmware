#pragma once

#include "carelink_gps_state.h"
#include "esp_err.h"

/* Start once from app_main, before other consumers. Owns UART1 and runs for
 * firmware lifetime. On failure resources are released and start may be retried.
 * No vendor commands, reset pulses, TX pin or sleep policy are configured. */
esp_err_t carelink_gps_start(void);

/* Thread-safe after successful start. Use state == CARELINK_GPS_FIXED before
 * using coordinates; fix.valid alone does not account for elapsed time. */
esp_err_t carelink_gps_get_snapshot(carelink_gps_snapshot_t *snapshot);
