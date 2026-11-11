#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CARELINK_AUTH_SHA256_SIZE 32U

/* Internal cryptographic backend boundary. Production uses ESP-IDF mbedTLS;
 * host tests provide an OpenSSL-backed implementation of the same primitive. */
bool carelink_auth_crypto_hmac_sha256(
    const uint8_t *key,
    size_t key_size,
    const uint8_t *input,
    size_t input_size,
    uint8_t output[CARELINK_AUTH_SHA256_SIZE]);
