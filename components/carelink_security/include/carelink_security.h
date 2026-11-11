#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "carelink_auth.h"
#include "esp_err.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_SECURITY_KEY_SIZE CARELINK_AUTH_PREFERRED_KEY_SIZE

typedef struct {
    nvs_handle_t handle;
    uint64_t next_counter;
    uint64_t reserved_through;
    bool initialized;
} carelink_security_t;

/* Opens the manufacturing-provisioned security namespace. NVS flash must be
 * initialized by the application. This function never creates a default key. */
esp_err_t carelink_security_init(carelink_security_t *security);

/* Returns the provider consumed by carelink_auth. The key must already exist
 * as a 32-byte `hmac_key` blob in namespace `carelink_sec`. */
carelink_auth_key_provider_t carelink_security_key_provider(
    carelink_security_t *security);

/* Uses the factory base MAC as a stable, non-secret 48-bit device identifier. */
esp_err_t carelink_security_device_id(uint64_t *device_id);

/* Returns a fresh nonzero per-boot session identifier from the hardware RNG. */
uint32_t carelink_security_new_session_id(void);

/* Allocates a never-reused monotonic value. NVS writes are amortized by a
 * reserved high-water window; skipped values after power loss are expected. */
esp_err_t carelink_security_next_replay_counter(carelink_security_t *security,
                                                uint64_t *counter);

#ifdef __cplusplus
}
#endif
