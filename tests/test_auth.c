#include "carelink_auth.h"
#include "carelink_auth_crypto.h"
#include "carelink_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t key[CARELINK_AUTH_MAX_KEY_SIZE];
    size_t key_size;
    size_t reported_size;
    carelink_auth_key_status_t status;
    unsigned calls;
    size_t observed_capacity;
} test_provider_t;

static carelink_auth_key_status_t get_test_key(
    void *context,
    uint8_t *key,
    size_t key_capacity,
    size_t *key_size)
{
    test_provider_t *state = (test_provider_t *)context;
    ++state->calls;
    state->observed_capacity = key_capacity;
    if (state->status != CARELINK_AUTH_PROVIDER_KEY_OK) {
        return state->status;
    }
    const size_t copy_size =
        state->key_size < key_capacity ? state->key_size : key_capacity;
    memcpy(key, state->key, copy_size);
    *key_size = state->reported_size != 0U ? state->reported_size
                                           : state->key_size;
    return CARELINK_AUTH_PROVIDER_KEY_OK;
}

static carelink_auth_key_provider_t make_provider(test_provider_t *state,
                                                   size_t key_size)
{
    memset(state, 0, sizeof(*state));
    state->key_size = key_size;
    state->status = CARELINK_AUTH_PROVIDER_KEY_OK;
    for (size_t index = 0U; index < sizeof(state->key); ++index) {
        state->key[index] = (uint8_t)(index + 1U);
    }
    const carelink_auth_key_provider_t provider = {
        .get_key = get_test_key,
        .context = state,
    };
    return provider;
}

static void initialize_frame(uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE])
{
    for (size_t index = 0U; index < CARELINK_PROTOCOL_FRAME_SIZE; ++index) {
        frame[index] = (uint8_t)(index * 3U + 1U);
    }
}

static void test_rfc4231_hmac_vector(void)
{
    static const uint8_t key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    };
    static const uint8_t input[] = "Hi There";
    static const uint8_t expected[CARELINK_AUTH_SHA256_SIZE] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
    };
    uint8_t output[CARELINK_AUTH_SHA256_SIZE] = {0};
    assert(carelink_auth_crypto_hmac_sha256(
        key, sizeof(key), input, sizeof(input) - 1U, output));
    assert(memcmp(output, expected, sizeof(expected)) == 0);
}

static void test_sign_verify_and_exact_boundary(void)
{
    static const uint8_t expected_tag[CARELINK_PROTOCOL_AUTH_TAG_SIZE] = {
        0xd8, 0x9d, 0x97, 0xba, 0x29, 0x29, 0x75, 0x9a,
        0x78, 0x27, 0x92, 0x01, 0x53, 0xb4, 0x29, 0x11,
    };
    test_provider_t state;
    const carelink_auth_key_provider_t provider =
        make_provider(&state, CARELINK_AUTH_PREFERRED_KEY_SIZE);
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    initialize_frame(frame);

    uint8_t original_prefix[CARELINK_PROTOCOL_AUTHENTICATED_SIZE];
    memcpy(original_prefix, frame, sizeof(original_prefix));
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_OK);
    assert(memcmp(frame, original_prefix, sizeof(original_prefix)) == 0);
    assert(memcmp(&frame[CARELINK_PROTOCOL_AUTHENTICATED_SIZE], expected_tag,
                  sizeof(expected_tag)) == 0);
    assert(carelink_auth_verify_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_OK);
    assert(state.calls == 2U);
    assert(state.observed_capacity == CARELINK_AUTH_MAX_KEY_SIZE);

    uint8_t signed_frame[CARELINK_PROTOCOL_FRAME_SIZE];
    memcpy(signed_frame, frame, sizeof(signed_frame));
    for (size_t index = 0U;
         index < CARELINK_PROTOCOL_AUTHENTICATED_SIZE; ++index) {
        frame[index] ^= UINT8_C(0x80);
        assert(carelink_auth_verify_frame(&provider, frame, sizeof(frame)) ==
               CARELINK_AUTH_FAILED);
        memcpy(frame, signed_frame, sizeof(frame));
    }
    for (size_t index = CARELINK_PROTOCOL_AUTHENTICATED_SIZE;
         index < CARELINK_PROTOCOL_FRAME_SIZE; ++index) {
        frame[index] ^= UINT8_C(0x01);
        assert(carelink_auth_verify_frame(&provider, frame, sizeof(frame)) ==
               CARELINK_AUTH_FAILED);
        memcpy(frame, signed_frame, sizeof(frame));
    }

    assert(carelink_auth_verify_frame(&provider, frame, sizeof(frame) - 1U) ==
           CARELINK_AUTH_INVALID_FRAME_SIZE);
    assert(carelink_auth_verify_frame(&provider, frame, sizeof(frame) + 1U) ==
           CARELINK_AUTH_INVALID_FRAME_SIZE);
}

static void test_provider_and_key_contracts(void)
{
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};
    initialize_frame(frame);
    uint8_t original_tag[CARELINK_PROTOCOL_AUTH_TAG_SIZE];
    memcpy(original_tag, &frame[CARELINK_PROTOCOL_AUTHENTICATED_SIZE],
           sizeof(original_tag));

    test_provider_t state;
    carelink_auth_key_provider_t provider =
        make_provider(&state, CARELINK_AUTH_MIN_KEY_SIZE - 1U);
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_KEY_TOO_SHORT);
    assert(memcmp(original_tag,
                  &frame[CARELINK_PROTOCOL_AUTHENTICATED_SIZE],
                  sizeof(original_tag)) == 0);

    provider = make_provider(&state, CARELINK_AUTH_MIN_KEY_SIZE);
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_OK);

    provider = make_provider(&state, CARELINK_AUTH_MAX_KEY_SIZE);
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_OK);

    provider = make_provider(&state, CARELINK_AUTH_MAX_KEY_SIZE);
    state.reported_size = CARELINK_AUTH_MAX_KEY_SIZE + 1U;
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_KEY_TOO_LONG);

    provider = make_provider(&state, CARELINK_AUTH_PREFERRED_KEY_SIZE);
    state.status = CARELINK_AUTH_PROVIDER_KEY_UNAVAILABLE;
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_KEY_UNAVAILABLE);
    assert(carelink_auth_verify_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_KEY_UNAVAILABLE);

    state.status = CARELINK_AUTH_PROVIDER_KEY_ERROR;
    assert(carelink_auth_sign_frame(&provider, frame, sizeof(frame)) ==
           CARELINK_AUTH_KEY_PROVIDER_ERROR);
}

static void test_invalid_arguments(void)
{
    test_provider_t state;
    const carelink_auth_key_provider_t provider =
        make_provider(&state, CARELINK_AUTH_PREFERRED_KEY_SIZE);
    const carelink_auth_key_provider_t no_callback = {0};
    uint8_t frame[CARELINK_PROTOCOL_FRAME_SIZE] = {0};

    assert(carelink_auth_sign_frame(NULL, frame, sizeof(frame)) ==
           CARELINK_AUTH_INVALID_ARGUMENT);
    assert(carelink_auth_sign_frame(&no_callback, frame, sizeof(frame)) ==
           CARELINK_AUTH_INVALID_ARGUMENT);
    assert(carelink_auth_sign_frame(&provider, NULL, sizeof(frame)) ==
           CARELINK_AUTH_INVALID_ARGUMENT);
    assert(carelink_auth_verify_frame(NULL, frame, sizeof(frame)) ==
           CARELINK_AUTH_INVALID_ARGUMENT);
    assert(carelink_auth_verify_frame(&no_callback, frame, sizeof(frame)) ==
           CARELINK_AUTH_INVALID_ARGUMENT);
    assert(carelink_auth_verify_frame(&provider, NULL, sizeof(frame)) ==
           CARELINK_AUTH_INVALID_ARGUMENT);
    assert(state.calls == 0U);
}

int main(void)
{
    test_rfc4231_hmac_vector();
    test_sign_verify_and_exact_boundary();
    test_provider_and_key_contracts();
    test_invalid_arguments();
    puts("authentication tests passed");
    return 0;
}
