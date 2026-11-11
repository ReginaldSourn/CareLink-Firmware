#include "carelink_sos_store.h"

#include <stddef.h>
#include <string.h>

#include "carelink_sos_store_record.h"

static const char *const NVS_NAMESPACE = "carelink_sos";
static const char *const SLOT_KEYS[2] = {"slot_a", "slot_b"};

typedef struct {
    uint8_t data[CARELINK_SOS_STORE_RECORD_SIZE];
    size_t size;
    bool present;
} stored_slot_t;

static esp_err_t read_slot(nvs_handle_t handle,
                           const char *key,
                           stored_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
    size_t stored_size = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &stored_size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    slot->present = true;
    slot->size = stored_size;
    if (stored_size != sizeof(slot->data)) {
        return ESP_OK;
    }
    size_t capacity = sizeof(slot->data);
    error = nvs_get_blob(handle, key, slot->data, &capacity);
    if (error != ESP_OK) {
        return error;
    }
    slot->size = capacity;
    return ESP_OK;
}

static esp_err_t scan_store(carelink_sos_store_t *store,
                            carelink_sos_store_selection_t *selection)
{
    stored_slot_t slots[2];
    esp_err_t error = read_slot(store->handle, SLOT_KEYS[0], &slots[0]);
    if (error != ESP_OK) {
        return error;
    }
    error = read_slot(store->handle, SLOT_KEYS[1], &slots[1]);
    if (error != ESP_OK) {
        return error;
    }
    const carelink_sos_store_record_view_t view_a = {
        .present = slots[0].present,
        .data = slots[0].data,
        .size = slots[0].size,
    };
    const carelink_sos_store_record_view_t view_b = {
        .present = slots[1].present,
        .data = slots[1].data,
        .size = slots[1].size,
    };
    *selection = carelink_sos_store_record_select(view_a, view_b);
    if (selection->result == CARELINK_SOS_STORE_SELECTION_CORRUPT) {
        return ESP_ERR_INVALID_CRC;
    }
    if (selection->result == CARELINK_SOS_STORE_SELECTION_INVALID_ARGUMENT) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void update_active(carelink_sos_store_t *store,
                          const carelink_sos_store_selection_t *selection)
{
    store->has_active_record =
        selection->result == CARELINK_SOS_STORE_SELECTION_VALID;
    if (store->has_active_record) {
        store->active_slot = (uint8_t)selection->slot;
        store->active_generation = selection->generation;
    } else {
        store->active_slot = (uint8_t)CARELINK_SOS_STORE_SLOT_A;
        store->active_generation = 0U;
    }
}

esp_err_t carelink_sos_store_init(carelink_sos_store_t *store)
{
    if (store == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(store, 0, sizeof(*store));
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &store->handle);
    if (error != ESP_OK) {
        return error;
    }
    store->initialized = true;

    carelink_sos_store_selection_t selection;
    error = scan_store(store, &selection);
    if (error == ESP_OK) {
        update_active(store, &selection);
    }
    return error;
}

esp_err_t carelink_sos_store_load(carelink_sos_store_t *store,
                                  carelink_sos_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    carelink_sos_store_selection_t selection;
    const esp_err_t error = scan_store(store, &selection);
    if (error != ESP_OK) {
        return error;
    }
    update_active(store, &selection);
    if (selection.result == CARELINK_SOS_STORE_SELECTION_EMPTY) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *snapshot = selection.snapshot;
    return ESP_OK;
}

esp_err_t carelink_sos_store_save(carelink_sos_store_t *store,
                                  const carelink_sos_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t generation =
        store->has_active_record ? store->active_generation + 1U : 1U;
    const carelink_sos_store_slot_t inactive_slot =
        store->has_active_record &&
                store->active_slot == (uint8_t)CARELINK_SOS_STORE_SLOT_A
            ? CARELINK_SOS_STORE_SLOT_B
            : CARELINK_SOS_STORE_SLOT_A;
    uint8_t record[CARELINK_SOS_STORE_RECORD_SIZE];
    const carelink_sos_store_record_result_t encode_result =
        carelink_sos_store_record_encode(snapshot, generation, record);
    if (encode_result == CARELINK_SOS_STORE_RECORD_INVALID_ARGUMENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (encode_result != CARELINK_SOS_STORE_RECORD_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = nvs_set_blob(store->handle, SLOT_KEYS[inactive_slot],
                                   record, sizeof(record));
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_commit(store->handle);
    if (error != ESP_OK) {
        return error;
    }
    store->active_slot = (uint8_t)inactive_slot;
    store->active_generation = generation;
    store->has_active_record = true;
    return ESP_OK;
}

esp_err_t carelink_sos_store_clear(carelink_sos_store_t *store)
{
    if (store == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0; index < 2U; ++index) {
        const esp_err_t error = nvs_erase_key(store->handle, SLOT_KEYS[index]);
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
            return error;
        }
    }
    const esp_err_t error = nvs_commit(store->handle);
    if (error != ESP_OK) {
        return error;
    }
    store->has_active_record = false;
    store->active_generation = 0U;
    store->active_slot = (uint8_t)CARELINK_SOS_STORE_SLOT_A;
    return ESP_OK;
}
