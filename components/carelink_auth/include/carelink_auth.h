#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HMAC-SHA-256 keys shorter than 128 bits are rejected. Provisioning should
 * use a unique, uniformly random 256-bit key for each device. */
#define CARELINK_AUTH_MIN_KEY_SIZE 16U
#define CARELINK_AUTH_PREFERRED_KEY_SIZE 32U
#define CARELINK_AUTH_MAX_KEY_SIZE 64U

typedef enum {
    CARELINK_AUTH_PROVIDER_KEY_OK = 0,
    CARELINK_AUTH_PROVIDER_KEY_UNAVAILABLE,
    CARELINK_AUTH_PROVIDER_KEY_ERROR,
} carelink_auth_key_status_t;

/* The provider copies the active per-device key into key and reports its byte
 * length through key_size. It must never return a shared/default key as a
 * fallback. key_capacity is CARELINK_AUTH_MAX_KEY_SIZE in current callers.
 * Implementations may retrieve the key from secure provisioning storage. */
typedef carelink_auth_key_status_t (*carelink_auth_get_key_fn)(
    void *context,
    uint8_t *key,
    size_t key_capacity,
    size_t *key_size);

typedef struct {
    carelink_auth_get_key_fn get_key;
    void *context;
} carelink_auth_key_provider_t;

typedef enum {
    CARELINK_AUTH_OK = 0,
    CARELINK_AUTH_INVALID_ARGUMENT,
    CARELINK_AUTH_INVALID_FRAME_SIZE,
    CARELINK_AUTH_KEY_UNAVAILABLE,
    CARELINK_AUTH_KEY_TOO_SHORT,
    CARELINK_AUTH_KEY_TOO_LONG,
    CARELINK_AUTH_KEY_PROVIDER_ERROR,
    CARELINK_AUTH_CRYPTO_ERROR,
    CARELINK_AUTH_FAILED,
} carelink_auth_result_t;

/* Signs exactly frame[0..CARELINK_PROTOCOL_AUTHENTICATED_SIZE) with
 * HMAC-SHA-256 and writes the truncated tag at the protocol tag offset.
 * The frame must be exactly CARELINK_PROTOCOL_FRAME_SIZE bytes. On failure,
 * the existing tag is left unchanged. */
carelink_auth_result_t carelink_auth_sign_frame(
    const carelink_auth_key_provider_t *provider,
    uint8_t *frame,
    size_t frame_size);

/* Verifies the frame tag in constant time after calculating HMAC-SHA-256 over
 * exactly frame[0..CARELINK_PROTOCOL_AUTHENTICATED_SIZE). A missing/invalid
 * key fails closed; no unauthenticated-success mode exists. */
carelink_auth_result_t carelink_auth_verify_frame(
    const carelink_auth_key_provider_t *provider,
    const uint8_t *frame,
    size_t frame_size);

#ifdef __cplusplus
}
#endif
