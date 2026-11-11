#include "carelink_auth.h"

#include "carelink_auth_crypto.h"
#include "carelink_protocol.h"

#include <stdbool.h>
#include <string.h>

_Static_assert(CARELINK_PROTOCOL_AUTH_TAG_SIZE <= CARELINK_AUTH_SHA256_SIZE,
               "protocol tag cannot exceed SHA-256 output");
_Static_assert(CARELINK_PROTOCOL_AUTHENTICATED_SIZE +
                       CARELINK_PROTOCOL_AUTH_TAG_SIZE ==
                   CARELINK_PROTOCOL_FRAME_SIZE,
               "authentication tag must immediately follow signed prefix");

static void secure_zero(void *buffer, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    while (size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static bool tags_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    volatile uint8_t difference = 0U;
    for (size_t index = 0U; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0U;
}

static carelink_auth_result_t load_key(
    const carelink_auth_key_provider_t *provider,
    uint8_t key[CARELINK_AUTH_MAX_KEY_SIZE],
    size_t *key_size)
{
    if (provider == NULL || provider->get_key == NULL || key_size == NULL) {
        return CARELINK_AUTH_INVALID_ARGUMENT;
    }

    *key_size = 0U;
    const carelink_auth_key_status_t status = provider->get_key(
        provider->context, key, CARELINK_AUTH_MAX_KEY_SIZE, key_size);
    if (status == CARELINK_AUTH_PROVIDER_KEY_UNAVAILABLE) {
        return CARELINK_AUTH_KEY_UNAVAILABLE;
    }
    if (status != CARELINK_AUTH_PROVIDER_KEY_OK) {
        return CARELINK_AUTH_KEY_PROVIDER_ERROR;
    }
    if (*key_size < CARELINK_AUTH_MIN_KEY_SIZE) {
        return CARELINK_AUTH_KEY_TOO_SHORT;
    }
    if (*key_size > CARELINK_AUTH_MAX_KEY_SIZE) {
        return CARELINK_AUTH_KEY_TOO_LONG;
    }
    return CARELINK_AUTH_OK;
}

static carelink_auth_result_t calculate_tag(
    const carelink_auth_key_provider_t *provider,
    const uint8_t *frame,
    uint8_t digest[CARELINK_AUTH_SHA256_SIZE])
{
    uint8_t key[CARELINK_AUTH_MAX_KEY_SIZE] = {0};
    size_t key_size = 0U;
    carelink_auth_result_t result = load_key(provider, key, &key_size);
    if (result == CARELINK_AUTH_OK &&
        !carelink_auth_crypto_hmac_sha256(
            key, key_size, frame, CARELINK_PROTOCOL_AUTHENTICATED_SIZE,
            digest)) {
        result = CARELINK_AUTH_CRYPTO_ERROR;
    }
    secure_zero(key, sizeof(key));
    key_size = 0U;
    return result;
}

carelink_auth_result_t carelink_auth_sign_frame(
    const carelink_auth_key_provider_t *provider,
    uint8_t *frame,
    size_t frame_size)
{
    if (provider == NULL || provider->get_key == NULL || frame == NULL) {
        return CARELINK_AUTH_INVALID_ARGUMENT;
    }
    if (frame_size != CARELINK_PROTOCOL_FRAME_SIZE) {
        return CARELINK_AUTH_INVALID_FRAME_SIZE;
    }

    uint8_t digest[CARELINK_AUTH_SHA256_SIZE] = {0};
    const carelink_auth_result_t result =
        calculate_tag(provider, frame, digest);
    if (result == CARELINK_AUTH_OK) {
        memcpy(&frame[CARELINK_PROTOCOL_AUTHENTICATED_SIZE], digest,
               CARELINK_PROTOCOL_AUTH_TAG_SIZE);
    }
    secure_zero(digest, sizeof(digest));
    return result;
}

carelink_auth_result_t carelink_auth_verify_frame(
    const carelink_auth_key_provider_t *provider,
    const uint8_t *frame,
    size_t frame_size)
{
    if (provider == NULL || provider->get_key == NULL || frame == NULL) {
        return CARELINK_AUTH_INVALID_ARGUMENT;
    }
    if (frame_size != CARELINK_PROTOCOL_FRAME_SIZE) {
        return CARELINK_AUTH_INVALID_FRAME_SIZE;
    }

    uint8_t digest[CARELINK_AUTH_SHA256_SIZE] = {0};
    carelink_auth_result_t result = calculate_tag(provider, frame, digest);
    if (result == CARELINK_AUTH_OK &&
        !tags_equal(digest,
                    &frame[CARELINK_PROTOCOL_AUTHENTICATED_SIZE],
                    CARELINK_PROTOCOL_AUTH_TAG_SIZE)) {
        result = CARELINK_AUTH_FAILED;
    }
    secure_zero(digest, sizeof(digest));
    return result;
}
