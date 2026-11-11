#include "carelink_radio.h"

#include "sdkconfig.h"

#if CONFIG_CARELINK_RADIO_ENABLED

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_CARELINK_RADIO_BOARD_HELTEC_V4_R8) && \
    defined(CONFIG_CARELINK_BOARD_HELTEC_V4_R2)
#error "The CareLink radio R8 profile conflicts with the CareLink R2 board profile"
#endif

#if (defined(CONFIG_CARELINK_RADIO_BOARD_HELTEC_V42_R2) || \
     defined(CONFIG_CARELINK_RADIO_BOARD_HELTEC_V431_R2)) && \
    defined(CONFIG_CARELINK_BOARD_HELTEC_V4_R8)
#error "The CareLink radio R2 profile conflicts with the CareLink R8 board profile"
#endif

enum {
    RADIO_PIN_NSS = 8,
    RADIO_PIN_SCK = 9,
    RADIO_PIN_MOSI = 10,
    RADIO_PIN_MISO = 11,
    RADIO_PIN_RESET = 12,
    RADIO_PIN_BUSY = 13,
    RADIO_PIN_DIO1 = 14,
    RADIO_PIN_FEM_POWER = 7,
    RADIO_PIN_FEM_ENABLE = 2,
};

#if CONFIG_CARELINK_RADIO_BOARD_HELTEC_V42_R2
#define RADIO_PROFILE_NAME "Heltec V4.2 S3R2 / GC1109"
enum { RADIO_PIN_FEM_MODE = 46, RADIO_FEM_RX_LEVEL = 1 };
#elif CONFIG_CARELINK_RADIO_BOARD_HELTEC_V431_R2
#define RADIO_PROFILE_NAME "Heltec V4.3.1 S3R2 / KCT8103L"
enum { RADIO_PIN_FEM_MODE = 5, RADIO_FEM_RX_LEVEL = 0 };
#elif CONFIG_CARELINK_RADIO_BOARD_HELTEC_V4_R8
#define RADIO_PROFILE_NAME "Heltec V4-R8 / KCT8103L"
enum { RADIO_PIN_FEM_MODE = 5, RADIO_FEM_RX_LEVEL = 0 };
#else
#error "Select exactly one CareLink radio board profile"
#endif

#define SX1262_CMD_SET_STANDBY 0x80
#define SX1262_CMD_SET_RX 0x82
#define SX1262_CMD_SET_TX 0x83
#define SX1262_CMD_CALIBRATE 0x89
#define SX1262_CMD_SET_PACKET_TYPE 0x8A
#define SX1262_CMD_SET_MODULATION_PARAMS 0x8B
#define SX1262_CMD_SET_PACKET_PARAMS 0x8C
#define SX1262_CMD_SET_TX_PARAMS 0x8E
#define SX1262_CMD_SET_BUFFER_BASE_ADDRESS 0x8F
#define SX1262_CMD_SET_RF_FREQUENCY 0x86
#define SX1262_CMD_SET_PA_CONFIG 0x95
#define SX1262_CMD_SET_DIO3_AS_TCXO_CTRL 0x97
#define SX1262_CMD_CALIBRATE_IMAGE 0x98
#define SX1262_CMD_SET_DIO2_AS_RF_SWITCH_CTRL 0x9D
#define SX1262_CMD_GET_STATUS 0xC0
#define SX1262_CMD_WRITE_REGISTER 0x0D
#define SX1262_CMD_WRITE_BUFFER 0x0E
#define SX1262_CMD_GET_IRQ_STATUS 0x12
#define SX1262_CMD_GET_RX_BUFFER_STATUS 0x13
#define SX1262_CMD_GET_PACKET_STATUS 0x14
#define SX1262_CMD_GET_DEVICE_ERRORS 0x17
#define SX1262_CMD_READ_BUFFER 0x1E
#define SX1262_CMD_CLEAR_DEVICE_ERRORS 0x07
#define SX1262_CMD_SET_DIO_IRQ_PARAMS 0x08
#define SX1262_CMD_CLEAR_IRQ_STATUS 0x02

#define SX1262_PACKET_TYPE_LORA 0x01
#define SX1262_STANDBY_RC 0x00
#define SX1262_LORA_BW_125 0x04
#define SX1262_LORA_CR_4_5 0x01
#define SX1262_IRQ_TX_DONE 0x0001
#define SX1262_IRQ_RX_DONE 0x0002
#define SX1262_IRQ_HEADER_ERROR 0x0020
#define SX1262_IRQ_CRC_ERROR 0x0040
#define SX1262_IRQ_TIMEOUT 0x0200
#define SX1262_RX_CONTINUOUS 0xFFFFFFU
#define SX1262_TCXO_1_8V 0x02
#define SX1262_TCXO_STARTUP_TICKS (10U * 64U)
#define SX1262_ALL_IRQS 0xFFFFU

static const char *TAG = "carelink_radio";
static spi_device_handle_t s_spi;
static TaskHandle_t s_owner;
static carelink_radio_config_t s_config;
static carelink_radio_status_t s_status = {
    .state = CARELINK_RADIO_STATE_UNINITIALIZED,
    .last_error = ESP_OK,
};

static esp_err_t require_owner(void)
{
    if (s_owner == NULL || s_status.state == CARELINK_RADIO_STATE_UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskGetCurrentTaskHandle() != s_owner) {
        ESP_LOGE(TAG, "radio accessed outside its owner task");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status.state == CARELINK_RADIO_STATE_TRANSMITTING) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

bool carelink_radio_config_valid(const carelink_radio_config_t *config)
{
    if (config == NULL || config->tx_power_dbm < -9 ||
        config->tx_power_dbm > 14) {
        return false;
    }
    const uint32_t frequency_hz = config->frequency_hz;
    return (frequency_hz >= 430000000U && frequency_hz <= 440000000U) ||
           (frequency_hz >= 470000000U && frequency_hz <= 510000000U) ||
           (frequency_hz >= 779000000U && frequency_hz <= 787000000U) ||
           (frequency_hz >= 863000000U && frequency_hz <= 870000000U) ||
           (frequency_hz >= 902000000U && frequency_hz <= 928000000U);
}

static esp_err_t validate_profile(void)
{
    const gpio_num_t output_pins[] = {
        RADIO_PIN_NSS, RADIO_PIN_SCK, RADIO_PIN_MOSI, RADIO_PIN_RESET,
        RADIO_PIN_FEM_POWER, RADIO_PIN_FEM_ENABLE, RADIO_PIN_FEM_MODE,
    };
    const gpio_num_t input_pins[] = {RADIO_PIN_MISO, RADIO_PIN_BUSY, RADIO_PIN_DIO1};

    for (size_t i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); ++i) {
        if (!GPIO_IS_VALID_OUTPUT_GPIO(output_pins[i])) {
            ESP_LOGE(TAG, "profile output GPIO%d is invalid", output_pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t i = 0; i < sizeof(input_pins) / sizeof(input_pins[0]); ++i) {
        if (!GPIO_IS_VALID_GPIO(input_pins[i])) {
            ESP_LOGE(TAG, "profile input GPIO%d is invalid", input_pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(output_pins) / sizeof(output_pins[0]); ++j) {
            if (output_pins[i] == output_pins[j]) {
                return ESP_ERR_INVALID_STATE;
            }
        }
        for (size_t j = 0; j < sizeof(input_pins) / sizeof(input_pins[0]); ++j) {
            if (output_pins[i] == input_pins[j]) {
                return ESP_ERR_INVALID_STATE;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t wait_while_busy(void)
{
    const int64_t started_us = esp_timer_get_time();
    while (gpio_get_level(RADIO_PIN_BUSY) != 0) {
        if (esp_timer_get_time() - started_us >=
            (int64_t)CONFIG_CARELINK_RADIO_BUSY_TIMEOUT_MS * 1000) {
            ESP_LOGE(TAG, "SX1262 BUSY timed out after %d ms",
                     CONFIG_CARELINK_RADIO_BUSY_TIMEOUT_MS);
            return ESP_ERR_TIMEOUT;
        }
        /* One raw tick always advances time, including with a 100 Hz tick. */
        vTaskDelay(1);
    }
    return ESP_OK;
}

static esp_err_t write_command(uint8_t opcode, const uint8_t *data, size_t length)
{
    uint8_t tx_data[257] = {0};
    if (length > sizeof(tx_data) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(wait_while_busy(), TAG, "BUSY before command 0x%02x", opcode);

    tx_data[0] = opcode;
    if (length > 0) {
        memcpy(&tx_data[1], data, length);
    }
    spi_transaction_t transaction = {
        .length = 8 * (length + 1),
        .tx_buffer = tx_data,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_spi, &transaction), TAG,
                        "SPI command 0x%02x failed", opcode);

    /* BUSY is permitted to assert up to 600 ns after NSS rises. */
    esp_rom_delay_us(1);
    return wait_while_busy();
}

static esp_err_t read_command(uint8_t opcode, uint8_t *data, size_t length)
{
    uint8_t tx_data[260] = {0};
    uint8_t rx_data[260] = {0};
    if (data == NULL || length > sizeof(tx_data) - 2) {
        return data == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(wait_while_busy(), TAG, "BUSY before read 0x%02x", opcode);

    tx_data[0] = opcode;
    spi_transaction_t transaction = {
        .length = 8 * (length + 2),
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_spi, &transaction), TAG,
                        "SPI read 0x%02x failed", opcode);
    memcpy(data, &rx_data[2], length);
    return wait_while_busy();
}

static esp_err_t read_buffer(uint8_t offset, uint8_t *data, size_t length)
{
    uint8_t tx_data[CARELINK_RADIO_MAX_PAYLOAD_SIZE + 3] = {0};
    uint8_t rx_data[CARELINK_RADIO_MAX_PAYLOAD_SIZE + 3] = {0};
    if (data == NULL || length > CARELINK_RADIO_MAX_PAYLOAD_SIZE) {
        return data == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(wait_while_busy(), TAG, "BUSY before FIFO read");

    tx_data[0] = SX1262_CMD_READ_BUFFER;
    tx_data[1] = offset;
    /* Byte two is the mandatory NOP/status byte; payload begins at byte three. */
    spi_transaction_t transaction = {
        .length = 8 * (length + 3),
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_spi, &transaction), TAG,
                        "FIFO read failed");
    memcpy(data, &rx_data[3], length);
    return wait_while_busy();
}

static esp_err_t check_radio_mode(uint8_t expected_mode, const char *stage)
{
    (void)stage; /* Used by production logging; host log stubs discard varargs. */
    uint8_t tx[] = {SX1262_CMD_GET_STATUS, 0x00};
    uint8_t rx[sizeof(tx)] = {0};
    ESP_RETURN_ON_ERROR(wait_while_busy(), TAG, "BUSY before status read");
    spi_transaction_t transaction = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_spi, &transaction), TAG,
                        "status read failed");

    const uint8_t status = rx[1];
    const uint8_t mode = (status >> 4) & 0x07;
    const uint8_t command_status = (status >> 1) & 0x07;
    if (status == 0x00 || status == 0xFF || mode != expected_mode ||
        (command_status >= 3 && command_status <= 5)) {
        ESP_LOGE(TAG, "%s: invalid status 0x%02x (mode=%u expected=%u command=%u)",
                 stage, status, mode, expected_mode, command_status);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t clear_irq_status(uint16_t mask)
{
    const uint8_t data[] = {(uint8_t)(mask >> 8), (uint8_t)mask};
    return write_command(SX1262_CMD_CLEAR_IRQ_STATUS, data, sizeof(data));
}

static esp_err_t set_frequency(uint32_t frequency_hz)
{
    const uint64_t value = ((uint64_t)frequency_hz << 25) / 32000000ULL;
    const uint8_t data[] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value,
    };
    return write_command(SX1262_CMD_SET_RF_FREQUENCY, data, sizeof(data));
}

static esp_err_t calibrate_image(uint32_t frequency_hz)
{
    uint8_t data[2];
    if (frequency_hz >= 430000000U && frequency_hz <= 440000000U) {
        data[0] = 0x6B; data[1] = 0x6F;
    } else if (frequency_hz >= 470000000U && frequency_hz <= 510000000U) {
        data[0] = 0x75; data[1] = 0x81;
    } else if (frequency_hz >= 779000000U && frequency_hz <= 787000000U) {
        data[0] = 0xC1; data[1] = 0xC5;
    } else if (frequency_hz >= 863000000U && frequency_hz <= 870000000U) {
        data[0] = 0xD7; data[1] = 0xDB;
    } else if (frequency_hz >= 902000000U && frequency_hz <= 928000000U) {
        data[0] = 0xE1; data[1] = 0xE9;
    } else {
        ESP_LOGE(TAG, "frequency %" PRIu32 " has no supported image-calibration band",
                 frequency_hz);
        return ESP_ERR_INVALID_ARG;
    }
    return write_command(SX1262_CMD_CALIBRATE_IMAGE, data, sizeof(data));
}

static esp_err_t enter_standby(void)
{
    const uint8_t standby[] = {SX1262_STANDBY_RC};
    ESP_RETURN_ON_ERROR(write_command(SX1262_CMD_SET_STANDBY, standby, sizeof(standby)),
                        TAG, "standby command failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(RADIO_PIN_FEM_MODE, RADIO_FEM_RX_LEVEL),
                        TAG, "FEM standby direction failed");
    s_status.state = CARELINK_RADIO_STATE_STANDBY;
    return ESP_OK;
}

static esp_err_t enter_receive(void)
{
    ESP_RETURN_ON_ERROR(enter_standby(), TAG, "RX standby failed");
    const uint16_t irq_mask = SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT |
                              SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR;
    const uint8_t irq_params[] = {
        (uint8_t)(irq_mask >> 8), (uint8_t)irq_mask,
        (uint8_t)(irq_mask >> 8), (uint8_t)irq_mask,
        0x00, 0x00, 0x00, 0x00,
    };
    ESP_RETURN_ON_ERROR(write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS,
                                      irq_params, sizeof(irq_params)),
                        TAG, "RX IRQ setup failed");
    ESP_RETURN_ON_ERROR(clear_irq_status(SX1262_ALL_IRQS), TAG, "RX IRQ clear failed");

    const uint8_t packet_params[] = {0x00, 0x08, 0x00, 0xFF, 0x01, 0x00};
    ESP_RETURN_ON_ERROR(write_command(SX1262_CMD_SET_PACKET_PARAMS,
                                      packet_params, sizeof(packet_params)),
                        TAG, "RX packet parameters failed");
    const uint8_t period[] = {
        (uint8_t)(SX1262_RX_CONTINUOUS >> 16),
        (uint8_t)(SX1262_RX_CONTINUOUS >> 8),
        (uint8_t)SX1262_RX_CONTINUOUS,
    };
    ESP_RETURN_ON_ERROR(write_command(SX1262_CMD_SET_RX, period, sizeof(period)),
                        TAG, "continuous RX failed");
    ESP_RETURN_ON_ERROR(check_radio_mode(5, "after SetRx"), TAG,
                        "radio did not enter receive mode");
    s_status.state = CARELINK_RADIO_STATE_RECEIVE_READY;
    return ESP_OK;
}

static esp_err_t restore_after_tx(carelink_radio_after_tx_t after_tx)
{
    if (after_tx == CARELINK_RADIO_AFTER_TX_RECEIVE) {
        return enter_receive();
    }
    ESP_RETURN_ON_ERROR(enter_standby(), TAG, "post-TX standby failed");
    return check_radio_mode(2, "after SetStandby");
}

esp_err_t carelink_radio_init_with_config(
    const carelink_radio_config_t *config)
{
    if (s_status.state != CARELINK_RADIO_STATE_UNINITIALIZED || s_owner != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(validate_profile(), TAG, "invalid radio profile");
    /* Validate before configuring GPIOs or starting the SPI bus. */
    if (!carelink_radio_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    s_owner = xTaskGetCurrentTaskHandle();
    s_status.state = CARELINK_RADIO_STATE_FAULT;

    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << RADIO_PIN_RESET) |
                        (1ULL << RADIO_PIN_FEM_POWER) |
                        (1ULL << RADIO_PIN_FEM_ENABLE) |
                        (1ULL << RADIO_PIN_FEM_MODE),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&outputs);
    if (err != ESP_OK) goto fail;

    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << RADIO_PIN_BUSY) | (1ULL << RADIO_PIN_DIO1),
        .mode = GPIO_MODE_INPUT,
    };
    err = gpio_config(&inputs);
    if (err != ESP_OK) goto fail;

    if ((err = gpio_set_level(RADIO_PIN_FEM_POWER, 1)) != ESP_OK ||
        (err = gpio_set_level(RADIO_PIN_FEM_ENABLE, 1)) != ESP_OK ||
        (err = gpio_set_level(RADIO_PIN_FEM_MODE, RADIO_FEM_RX_LEVEL)) != ESP_OK) {
        goto fail;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = RADIO_PIN_SCK,
        .mosi_io_num = RADIO_PIN_MOSI,
        .miso_io_num = RADIO_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 260,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) goto fail;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = RADIO_PIN_NSS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &device_config, &s_spi);
    if (err != ESP_OK) goto fail;

    gpio_set_level(RADIO_PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RADIO_PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RADIO_PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    if ((err = wait_while_busy()) != ESP_OK) goto fail;

    if ((err = enter_standby()) != ESP_OK) goto fail;
    if ((err = check_radio_mode(2, "after reset")) != ESP_OK) goto fail;

    const uint32_t tcxo_ticks = SX1262_TCXO_STARTUP_TICKS;
    const uint8_t tcxo[] = {
        SX1262_TCXO_1_8V, (uint8_t)(tcxo_ticks >> 16),
        (uint8_t)(tcxo_ticks >> 8), (uint8_t)tcxo_ticks,
    };
    if ((err = write_command(SX1262_CMD_SET_DIO3_AS_TCXO_CTRL,
                             tcxo, sizeof(tcxo))) != ESP_OK) goto fail;

    const uint8_t clear_errors[] = {0x00, 0x00};
    if ((err = write_command(SX1262_CMD_CLEAR_DEVICE_ERRORS,
                             clear_errors, sizeof(clear_errors))) != ESP_OK) goto fail;
    const uint8_t calibrate_all[] = {0x7F};
    if ((err = write_command(SX1262_CMD_CALIBRATE,
                             calibrate_all, sizeof(calibrate_all))) != ESP_OK) goto fail;
    if ((err = check_radio_mode(2, "after calibration")) != ESP_OK) goto fail;

    const uint8_t rf_switch[] = {0x01};
    if ((err = write_command(SX1262_CMD_SET_DIO2_AS_RF_SWITCH_CTRL,
                             rf_switch, sizeof(rf_switch))) != ESP_OK) goto fail;
    const uint8_t packet_type[] = {SX1262_PACKET_TYPE_LORA};
    if ((err = write_command(SX1262_CMD_SET_PACKET_TYPE,
                             packet_type, sizeof(packet_type))) != ESP_OK) goto fail;
    if ((err = calibrate_image(s_config.frequency_hz)) != ESP_OK) goto fail;

    uint8_t device_errors[2];
    if ((err = read_command(SX1262_CMD_GET_DEVICE_ERRORS,
                            device_errors, sizeof(device_errors))) != ESP_OK) goto fail;
    if (device_errors[0] != 0 || device_errors[1] != 0) {
        ESP_LOGE(TAG, "SX1262 calibration errors 0x%02x%02x",
                 device_errors[0], device_errors[1]);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }

    if ((err = set_frequency(s_config.frequency_hz)) != ESP_OK) goto fail;
    const uint8_t buffer_base[] = {0x00, 0x00};
    if ((err = write_command(SX1262_CMD_SET_BUFFER_BASE_ADDRESS,
                             buffer_base, sizeof(buffer_base))) != ESP_OK) goto fail;

    /* Rolork gateway contract: BW125, SF9, CR4/5. LDRO is off at 4.096 ms. */
    const uint8_t modulation[] = {0x09, SX1262_LORA_BW_125, SX1262_LORA_CR_4_5, 0x00};
    if ((err = write_command(SX1262_CMD_SET_MODULATION_PARAMS,
                             modulation, sizeof(modulation))) != ESP_OK) goto fail;
    /* Preamble 8, explicit header, max payload 255, CRC on, normal IQ. */
    const uint8_t packet[] = {0x00, 0x08, 0x00, 0xFF, 0x01, 0x00};
    if ((err = write_command(SX1262_CMD_SET_PACKET_PARAMS,
                             packet, sizeof(packet))) != ESP_OK) goto fail;

    /* SX1262 low-power PA operating point (up to +14 dBm), 200 us ramp. */
    const uint8_t pa[] = {0x02, 0x02, 0x00, 0x01};
    if ((err = write_command(SX1262_CMD_SET_PA_CONFIG, pa, sizeof(pa))) != ESP_OK) goto fail;
    const uint8_t tx_params[] = {(uint8_t)s_config.tx_power_dbm, 0x04};
    if ((err = write_command(SX1262_CMD_SET_TX_PARAMS,
                             tx_params, sizeof(tx_params))) != ESP_OK) goto fail;

    s_status.state = CARELINK_RADIO_STATE_STANDBY;
    s_status.last_error = ESP_OK;
    ESP_LOGW(TAG, "raw LoRa at %" PRIu32 " Hz requires deployment-region approval",
             s_config.frequency_hz);
    ESP_LOGI(TAG, "%s ready: BW125 SF9 CR4/5 preamble8 explicit CRC, TX %d dBm",
             RADIO_PROFILE_NAME, s_config.tx_power_dbm);
    return ESP_OK;

fail:
    s_status.state = CARELINK_RADIO_STATE_FAULT;
    s_status.last_error = err;
    return err;
}

esp_err_t carelink_radio_init(void)
{
    const carelink_radio_config_t config = {
        .frequency_hz = CONFIG_CARELINK_RADIO_FREQUENCY_HZ,
        .tx_power_dbm = CONFIG_CARELINK_RADIO_TX_POWER_DBM,
    };
    return carelink_radio_init_with_config(&config);
}

esp_err_t carelink_radio_send(const uint8_t *payload, size_t length,
                              carelink_radio_after_tx_t after_tx)
{
    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length == 0 || length > CARELINK_RADIO_MAX_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (after_tx != CARELINK_RADIO_AFTER_TX_STANDBY &&
        after_tx != CARELINK_RADIO_AFTER_TX_RECEIVE) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = require_owner();
    if (err != ESP_OK) {
        return err;
    }

    err = enter_standby();
    if (err != ESP_OK) goto finish;
    s_status.state = CARELINK_RADIO_STATE_TRANSMITTING;

    uint8_t fifo[CARELINK_RADIO_MAX_PAYLOAD_SIZE + 1] = {0};
    memcpy(&fifo[1], payload, length);
    err = write_command(SX1262_CMD_WRITE_BUFFER, fifo, length + 1);
    if (err != ESP_OK) goto finish;

    const uint8_t packet[] = {
        0x00, 0x08, 0x00, (uint8_t)length, 0x01, 0x00,
    };
    err = write_command(SX1262_CMD_SET_PACKET_PARAMS, packet, sizeof(packet));
    if (err != ESP_OK) goto finish;

    const uint16_t irq_mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT;
    const uint8_t irq_params[] = {
        (uint8_t)(irq_mask >> 8), (uint8_t)irq_mask,
        (uint8_t)(irq_mask >> 8), (uint8_t)irq_mask,
        0x00, 0x00, 0x00, 0x00,
    };
    err = write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_params, sizeof(irq_params));
    if (err != ESP_OK) goto finish;
    err = clear_irq_status(SX1262_ALL_IRQS);
    if (err != ESP_OK) goto finish;

    err = gpio_set_level(RADIO_PIN_FEM_MODE, !RADIO_FEM_RX_LEVEL);
    if (err != ESP_OK) goto finish;

    const uint32_t radio_timeout = (uint32_t)CONFIG_CARELINK_RADIO_TX_TIMEOUT_MS * 64U;
    const uint8_t tx_timeout[] = {
        (uint8_t)(radio_timeout >> 16), (uint8_t)(radio_timeout >> 8),
        (uint8_t)radio_timeout,
    };
    err = write_command(SX1262_CMD_SET_TX, tx_timeout, sizeof(tx_timeout));
    if (err != ESP_OK) goto finish;

    const int64_t started_us = esp_timer_get_time();
    const int64_t host_timeout_us =
        ((int64_t)CONFIG_CARELINK_RADIO_TX_TIMEOUT_MS + 100) * 1000;
    while (gpio_get_level(RADIO_PIN_DIO1) == 0) {
        if (esp_timer_get_time() - started_us >= host_timeout_us) {
            err = ESP_ERR_TIMEOUT;
            goto finish;
        }
        vTaskDelay(1);
    }

    uint8_t irq[2];
    err = read_command(SX1262_CMD_GET_IRQ_STATUS, irq, sizeof(irq));
    if (err != ESP_OK) goto finish;
    const uint16_t flags = ((uint16_t)irq[0] << 8) | irq[1];
    if ((flags & SX1262_IRQ_TIMEOUT) != 0) {
        err = ESP_ERR_TIMEOUT;
    } else if ((flags & SX1262_IRQ_TX_DONE) == 0) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        err = clear_irq_status(flags);
    } else {
        (void)clear_irq_status(flags);
    }

finish:
    {
        const esp_err_t tx_err = err;
        const esp_err_t recovery_err = restore_after_tx(after_tx);
        if (tx_err == ESP_OK && recovery_err == ESP_OK) {
            s_status.successful_packets++;
            s_status.last_error = ESP_OK;
            return ESP_OK;
        }
        s_status.failed_packets++;
        s_status.last_error = tx_err != ESP_OK ? tx_err : recovery_err;
        if (recovery_err != ESP_OK) {
            s_status.state = CARELINK_RADIO_STATE_FAULT;
            ESP_LOGE(TAG, "post-TX recovery failed: %s", esp_err_to_name(recovery_err));
        }
        return s_status.last_error;
    }
}

esp_err_t carelink_radio_start_receive(void)
{
    ESP_RETURN_ON_ERROR(require_owner(), TAG, "receive requested by non-owner");
    const esp_err_t err = enter_receive();
    s_status.last_error = err;
    if (err != ESP_OK) {
        s_status.state = CARELINK_RADIO_STATE_FAULT;
    }
    return err;
}

esp_err_t carelink_radio_receive(carelink_radio_packet_t *packet,
                                 bool *packet_ready, uint32_t timeout_ms)
{
    if (packet == NULL || packet_ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timeout_ms > CONFIG_CARELINK_RADIO_RX_MAX_WAIT_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(require_owner(), TAG, "receive poll requested by non-owner");
    if (s_status.state != CARELINK_RADIO_STATE_RECEIVE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    *packet_ready = false;
    packet->length = 0;
    packet->rssi_dbm = 0;
    packet->snr_db = 0;

    const int64_t started_us = esp_timer_get_time();
    const int64_t deadline_us = (int64_t)timeout_ms * 1000;
    while (gpio_get_level(RADIO_PIN_DIO1) == 0) {
        if (timeout_ms == 0 || esp_timer_get_time() - started_us >= deadline_us) {
            s_status.last_error = ESP_OK;
            return ESP_OK;
        }
        vTaskDelay(1);
    }

    esp_err_t event_err = ESP_OK;
    uint16_t flags = 0;
    uint8_t irq[2];
    event_err = read_command(SX1262_CMD_GET_IRQ_STATUS, irq, sizeof(irq));
    if (event_err == ESP_OK) {
        flags = ((uint16_t)irq[0] << 8) | irq[1];
        if ((flags & SX1262_IRQ_TIMEOUT) != 0) {
            event_err = ESP_ERR_TIMEOUT;
        } else if ((flags & SX1262_IRQ_CRC_ERROR) != 0) {
            event_err = ESP_ERR_INVALID_CRC;
        } else if ((flags & SX1262_IRQ_HEADER_ERROR) != 0) {
            event_err = ESP_ERR_INVALID_RESPONSE;
        } else if ((flags & SX1262_IRQ_RX_DONE) == 0) {
            event_err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    if (event_err == ESP_OK) {
        uint8_t buffer_status[2];
        event_err = read_command(SX1262_CMD_GET_RX_BUFFER_STATUS,
                                 buffer_status, sizeof(buffer_status));
        if (event_err == ESP_OK) {
            packet->length = buffer_status[0];
            if (packet->length == 0U) {
                event_err = ESP_ERR_INVALID_SIZE;
            } else {
                event_err = read_buffer(buffer_status[1], packet->data,
                                        packet->length);
            }
        }
    }

    if (event_err == ESP_OK) {
        uint8_t packet_status[3];
        event_err = read_command(SX1262_CMD_GET_PACKET_STATUS,
                                 packet_status, sizeof(packet_status));
        if (event_err == ESP_OK) {
            packet->rssi_dbm = -(int16_t)packet_status[0] / 2;
            packet->snr_db = ((int8_t)packet_status[1]) / 4;
        }
    }

    esp_err_t clear_err = clear_irq_status(flags == 0 ? SX1262_ALL_IRQS : flags);
    const esp_err_t recovery_err = enter_receive();
    esp_err_t result = event_err;
    if (result == ESP_OK) result = clear_err;
    if (result == ESP_OK) result = recovery_err;

    if (result == ESP_OK) {
        s_status.received_packets++;
        s_status.last_error = ESP_OK;
        *packet_ready = true;
        return ESP_OK;
    }

    s_status.receive_errors++;
    s_status.last_error = result;
    packet->length = 0;
    if (recovery_err != ESP_OK) {
        s_status.state = CARELINK_RADIO_STATE_FAULT;
        ESP_LOGE(TAG, "post-RX recovery failed: %s", esp_err_to_name(recovery_err));
    }
    return result;
}

carelink_radio_status_t carelink_radio_get_status(void)
{
    return s_status;
}

#else

bool carelink_radio_config_valid(const carelink_radio_config_t *config)
{
    (void)config;
    return false;
}

esp_err_t carelink_radio_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_radio_init_with_config(
    const carelink_radio_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_radio_send(const uint8_t *payload, size_t length,
                              carelink_radio_after_tx_t after_tx)
{
    (void)payload;
    (void)length;
    (void)after_tx;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_radio_start_receive(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_radio_receive(carelink_radio_packet_t *packet,
                                 bool *packet_ready, uint32_t timeout_ms)
{
    (void)packet;
    (void)packet_ready;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

carelink_radio_status_t carelink_radio_get_status(void)
{
    const carelink_radio_status_t status = {
        .state = CARELINK_RADIO_STATE_UNINITIALIZED,
        .last_error = ESP_ERR_NOT_SUPPORTED,
    };
    return status;
}

#endif
