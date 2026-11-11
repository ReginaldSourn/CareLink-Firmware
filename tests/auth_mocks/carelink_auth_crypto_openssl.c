#include "carelink_auth_crypto.h"

#include <openssl/evp.h>

bool carelink_auth_crypto_hmac_sha256(
    const uint8_t *key,
    size_t key_size,
    const uint8_t *input,
    size_t input_size,
    uint8_t output[CARELINK_AUTH_SHA256_SIZE])
{
    size_t output_size = 0U;
    return EVP_Q_mac(NULL, "HMAC", NULL, "SHA256", NULL, key, key_size,
                     input, input_size, output, CARELINK_AUTH_SHA256_SIZE,
                     &output_size) != NULL &&
           output_size == CARELINK_AUTH_SHA256_SIZE;
}
