#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARELINK_RADIO_MAX_PAYLOAD_SIZE 255U

/** State of the single-owner, blocking radio driver. */
typedef enum {
    CARELINK_RADIO_STATE_UNINITIALIZED = 0,
    CARELINK_RADIO_STATE_STANDBY,
    CARELINK_RADIO_STATE_TRANSMITTING,
    CARELINK_RADIO_STATE_RECEIVE_READY,
    CARELINK_RADIO_STATE_FAULT,
} carelink_radio_state_t;

/** Mode selected after a successful or failed blocking transmit attempt. */
typedef enum {
    CARELINK_RADIO_AFTER_TX_STANDBY = 0,
    CARELINK_RADIO_AFTER_TX_RECEIVE,
} carelink_radio_after_tx_t;

typedef struct {
    size_t length;
    int16_t rssi_dbm;
    int8_t snr_db;
    uint8_t data[CARELINK_RADIO_MAX_PAYLOAD_SIZE];
} carelink_radio_packet_t;

typedef struct {
    carelink_radio_state_t state;
    esp_err_t last_error;
    uint32_t successful_packets;
    uint32_t failed_packets;
    uint32_t received_packets;
    uint32_t receive_errors;
} carelink_radio_status_t;

typedef struct {
    uint32_t frequency_hz;
    int8_t tx_power_dbm;
} carelink_radio_config_t;

bool carelink_radio_config_valid(const carelink_radio_config_t *config);

/**
 * Initialize the on-board SX1262 and claim it for the calling FreeRTOS task.
 *
 * Every subsequent operation must be called from the same task. The function
 * leaves the radio in STDBY_RC and is not safe to call more than once.
 */
esp_err_t carelink_radio_init(void);

/* Runtime-configurable variant used by the incident service. Hardware pin and
 * RF-front-end selection remain compile-time board-profile decisions. */
esp_err_t carelink_radio_init_with_config(
    const carelink_radio_config_t *config);

/**
 * Transmit one binary raw-LoRa packet and block until TX_DONE or timeout.
 *
 * Valid payload lengths are 1..CARELINK_RADIO_MAX_PAYLOAD_SIZE. This function
 * must be called by the task that called carelink_radio_init(). It always
 * attempts to leave the radio in the requested post-TX mode, including after
 * a transmit error. If recovery also fails, the state becomes FAULT.
 */
esp_err_t carelink_radio_send(const uint8_t *payload, size_t length,
                              carelink_radio_after_tx_t after_tx);

/** Enter continuous raw-LoRa receive mode without consuming packets. */
esp_err_t carelink_radio_start_receive(void);

/**
 * Poll for one binary packet while in RECEIVE_READY.
 *
 * A timeout of zero performs one nonblocking DIO1 check. Positive timeouts are
 * bounded by CONFIG_CARELINK_RADIO_RX_MAX_WAIT_MS. ESP_OK with packet_ready
 * false means no packet arrived before the host deadline. Radio timeout, CRC,
 * and malformed-header IRQs return ESP_ERR_TIMEOUT, ESP_ERR_INVALID_CRC, and
 * ESP_ERR_INVALID_RESPONSE respectively. The driver clears the IRQ and
 * restores continuous RX after every radio event.
 */
esp_err_t carelink_radio_receive(carelink_radio_packet_t *packet,
                                 bool *packet_ready, uint32_t timeout_ms);

/** Return the most recent driver state and counters without touching hardware. */
carelink_radio_status_t carelink_radio_get_status(void);

#ifdef __cplusplus
}
#endif
