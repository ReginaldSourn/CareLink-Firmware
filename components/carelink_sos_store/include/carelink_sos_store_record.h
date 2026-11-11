#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "carelink_sos.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_SOS_STORE_RECORD_SIZE 44U
#define CARELINK_SOS_STORE_SCHEMA_VERSION 1U

typedef enum {
    CARELINK_SOS_STORE_RECORD_OK = 0,
    CARELINK_SOS_STORE_RECORD_INVALID_ARGUMENT,
    CARELINK_SOS_STORE_RECORD_INVALID_SIZE,
    CARELINK_SOS_STORE_RECORD_INVALID_MAGIC,
    CARELINK_SOS_STORE_RECORD_UNSUPPORTED_SCHEMA,
    CARELINK_SOS_STORE_RECORD_INVALID_CRC,
    CARELINK_SOS_STORE_RECORD_INVALID_FIELD,
} carelink_sos_store_record_result_t;

typedef enum {
    CARELINK_SOS_STORE_SLOT_A = 0,
    CARELINK_SOS_STORE_SLOT_B = 1,
} carelink_sos_store_slot_t;

typedef struct {
    bool present;
    const uint8_t *data;
    size_t size;
} carelink_sos_store_record_view_t;

typedef enum {
    CARELINK_SOS_STORE_SELECTION_EMPTY = 0,
    CARELINK_SOS_STORE_SELECTION_VALID,
    CARELINK_SOS_STORE_SELECTION_CORRUPT,
    CARELINK_SOS_STORE_SELECTION_INVALID_ARGUMENT,
} carelink_sos_store_selection_result_t;

typedef struct {
    carelink_sos_store_selection_result_t result;
    carelink_sos_store_slot_t slot;
    uint32_t generation;
    carelink_sos_snapshot_t snapshot;
} carelink_sos_store_selection_t;

carelink_sos_store_record_result_t carelink_sos_store_record_encode(
    const carelink_sos_snapshot_t *snapshot,
    uint32_t generation,
    uint8_t record[CARELINK_SOS_STORE_RECORD_SIZE]);

/* On failure, snapshot and generation are left unchanged. */
carelink_sos_store_record_result_t carelink_sos_store_record_decode(
    const uint8_t *record,
    size_t record_size,
    carelink_sos_snapshot_t *snapshot,
    uint32_t *generation);

/* Selects the newest valid record. A malformed peer never masks a valid slot.
 * When equally generated records exist, slot A wins deterministically. */
carelink_sos_store_selection_t carelink_sos_store_record_select(
    carelink_sos_store_record_view_t slot_a,
    carelink_sos_store_record_view_t slot_b);

#ifdef __cplusplus
}
#endif
