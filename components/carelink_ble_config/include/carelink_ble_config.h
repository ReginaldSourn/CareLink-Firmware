#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts an authenticated BLE Local Control service. The service is disabled
 * at compile time unless CONFIG_CARELINK_BLE_CONFIG_ENABLED is selected. */
esp_err_t carelink_ble_config_start(void);

#ifdef __cplusplus
}
#endif
