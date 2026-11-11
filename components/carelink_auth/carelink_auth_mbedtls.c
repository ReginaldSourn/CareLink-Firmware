#include "carelink_auth_crypto.h"

#include "psa/crypto.h"

bool carelink_auth_crypto_hmac_sha256(
    const uint8_t *key,
    size_t key_size,
    const uint8_t *input,
    size_t input_size,
    uint8_t output[CARELINK_AUTH_SHA256_SIZE])
{
    if (key == NULL || key_size == 0U || input == NULL || output == NULL ||
        psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, key_size * 8U);

    psa_key_id_t key_id = 0;
    psa_status_t status = psa_import_key(&attributes, key, key_size, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return false;
    }

    size_t output_size = 0U;
    status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256), input,
                             input_size, output, CARELINK_AUTH_SHA256_SIZE,
                             &output_size);
    const psa_status_t destroy_status = psa_destroy_key(key_id);
    return status == PSA_SUCCESS &&
           output_size == CARELINK_AUTH_SHA256_SIZE &&
           destroy_status == PSA_SUCCESS;
}
