#pragma once

#include "carelink_config_codec.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARELINK_CONFIG_SOURCE_DEFAULTS = 0,
    CARELINK_CONFIG_SOURCE_RUNTIME,
    CARELINK_CONFIG_SOURCE_RECOVERY,
} carelink_config_source_t;

void carelink_config_defaults(carelink_runtime_config_t *config);

/* NVS flash must already be initialized. Missing data uses compile-time
 * defaults. Corrupt data also fails safely to defaults and reports RECOVERY. */
esp_err_t carelink_config_load(carelink_runtime_config_t *config,
                               carelink_config_source_t *source);

/* Atomically writes the inactive CRC-protected slot and commits it. */
esp_err_t carelink_config_save(const carelink_runtime_config_t *config);

#ifdef __cplusplus
}
#endif
