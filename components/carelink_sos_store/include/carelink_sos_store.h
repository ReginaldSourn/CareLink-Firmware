#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "carelink_sos.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nvs_handle_t handle;
    uint32_t active_generation;
    uint8_t active_slot;
    bool initialized;
    bool has_active_record;
} carelink_sos_store_t;

/* NVS flash itself must already be initialized. A corrupt journal returns
 * ESP_ERR_INVALID_CRC but leaves the handle usable for clear or a later save. */
esp_err_t carelink_sos_store_init(carelink_sos_store_t *store);

/* Returns ESP_ERR_NVS_NOT_FOUND for a clean, empty journal. Corruption returns
 * ESP_ERR_INVALID_CRC. In either case, snapshot is left unchanged. */
esp_err_t carelink_sos_store_load(carelink_sos_store_t *store,
                                  carelink_sos_snapshot_t *snapshot);

/* Writes the inactive slot and commits it before changing active metadata.
 * A failure is returned to the caller; emergency transmission need not stop. */
esp_err_t carelink_sos_store_save(carelink_sos_store_t *store,
                                  const carelink_sos_snapshot_t *snapshot);

esp_err_t carelink_sos_store_clear(carelink_sos_store_t *store);

#ifdef __cplusplus
}
#endif
