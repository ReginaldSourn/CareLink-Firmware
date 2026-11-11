#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../components/carelink_radio/carelink_radio.c"

static int64_t fake_time_us;
static int64_t busy_until_us;
static int gpio_levels[49];
static TaskHandle_t current_task = (void *)1;
static uint8_t radio_mode;
static uint16_t radio_irq;
static bool tx_should_complete;
static uint8_t transmitted[255];
static size_t transmitted_length;
static uint8_t receive_fifo[256];
static uint8_t receive_length;
static uint8_t receive_offset;
static uint8_t receive_rssi_raw;
static int8_t receive_snr_raw;
static unsigned set_tx_count;
static unsigned set_rx_count;
static bool tcxo_configured;
static bool calibration_ran;

const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "test error";
}

int64_t esp_timer_get_time(void)
{
    return fake_time_us;
}

void esp_rom_delay_us(uint32_t microseconds)
{
    fake_time_us += microseconds;
}

void vTaskDelay(TickType_t ticks)
{
    fake_time_us += ticks == 0 ? 1 :
                    (int64_t)ticks * (1000000 / TEST_TICK_HZ);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return current_task;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    assert(pin >= 0 && pin < (int)(sizeof(gpio_levels) / sizeof(gpio_levels[0])));
    gpio_levels[pin] = (int)level;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t pin)
{
    if (pin == RADIO_PIN_BUSY) {
        return fake_time_us < busy_until_us;
    }
    if (pin == RADIO_PIN_DIO1) {
        if (radio_mode == 6 && tx_should_complete && set_tx_count > 0) {
            radio_irq = SX1262_IRQ_TX_DONE;
            radio_mode = 2;
            return 1;
        }
        return radio_mode == 5 && radio_irq != 0;
    }
    return gpio_levels[pin];
}

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *config, int dma_channel)
{
    assert(host == SPI2_HOST);
    assert(config->sclk_io_num == 9 && config->mosi_io_num == 10);
    assert(config->miso_io_num == 11 && config->max_transfer_sz >= 260);
    assert(dma_channel == SPI_DMA_CH_AUTO);
    return ESP_OK;
}

esp_err_t spi_bus_add_device(spi_host_device_t host,
                             const spi_device_interface_config_t *config,
                             spi_device_handle_t *device)
{
    assert(host == SPI2_HOST && config->spics_io_num == 8);
    *device = (void *)2;
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t device,
                                      spi_transaction_t *transaction)
{
    assert(device == (void *)2);
    assert(fake_time_us >= busy_until_us);
    const uint8_t *tx = transaction->tx_buffer;
    uint8_t *rx = transaction->rx_buffer;
    const size_t bytes = transaction->length / 8;
    if (rx != NULL) memset(rx, 0, bytes);

    switch (tx[0]) {
    case SX1262_CMD_GET_STATUS:
        assert(bytes == 2 && rx != NULL);
        rx[1] = (uint8_t)((radio_mode << 4) | (2 << 1));
        break;
    case SX1262_CMD_SET_STANDBY:
        assert(bytes == 2 && tx[1] == SX1262_STANDBY_RC);
        radio_mode = 2;
        break;
    case SX1262_CMD_SET_DIO3_AS_TCXO_CTRL:
        assert(bytes == 5 && tx[1] == SX1262_TCXO_1_8V);
        tcxo_configured = true;
        break;
    case SX1262_CMD_CALIBRATE:
        assert(tcxo_configured && bytes == 2 && tx[1] == 0x7F);
        calibration_ran = true;
        busy_until_us = fake_time_us + 13000;
        break;
    case SX1262_CMD_GET_DEVICE_ERRORS:
        assert(calibration_ran && rx != NULL && bytes == 4);
        break;
    case SX1262_CMD_SET_MODULATION_PARAMS:
        assert(bytes == 5);
        assert(tx[1] == 9 && tx[2] == 4 && tx[3] == 1 && tx[4] == 0);
        break;
    case SX1262_CMD_SET_PACKET_PARAMS:
        assert(bytes == 7);
        assert(tx[1] == 0 && tx[2] == 8 && tx[3] == 0);
        assert(tx[5] == 1 && tx[6] == 0);
        break;
    case SX1262_CMD_SET_PA_CONFIG:
        assert(bytes == 5 && tx[1] == 2 && tx[2] == 2);
        break;
    case SX1262_CMD_SET_TX_PARAMS:
        assert(bytes == 3 && tx[1] == 0 && tx[2] == 4);
        break;
    case SX1262_CMD_WRITE_BUFFER:
        assert(bytes >= 3 && bytes <= 257 && tx[1] == 0);
        transmitted_length = bytes - 2;
        memcpy(transmitted, &tx[2], transmitted_length);
        break;
    case SX1262_CMD_SET_TX:
        assert(bytes == 4);
        assert((((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3]) ==
               CONFIG_CARELINK_RADIO_TX_TIMEOUT_MS * 64U);
        assert(gpio_levels[RADIO_PIN_FEM_MODE] == !RADIO_FEM_RX_LEVEL);
        radio_mode = 6;
        set_tx_count++;
        break;
    case SX1262_CMD_GET_IRQ_STATUS:
        assert(bytes == 4 && rx != NULL);
        rx[2] = (uint8_t)(radio_irq >> 8);
        rx[3] = (uint8_t)radio_irq;
        break;
    case SX1262_CMD_SET_DIO_IRQ_PARAMS: {
        assert(bytes == 9);
        const uint16_t mask = ((uint16_t)tx[1] << 8) | tx[2];
        const uint16_t dio1 = ((uint16_t)tx[3] << 8) | tx[4];
        assert(mask == dio1);
        assert(mask == (SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT) ||
               mask == (SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT |
                        SX1262_IRQ_CRC_ERROR | SX1262_IRQ_HEADER_ERROR));
        break;
    }
    case SX1262_CMD_GET_RX_BUFFER_STATUS:
        assert(bytes == 4 && rx != NULL);
        rx[2] = receive_length;
        rx[3] = receive_offset;
        break;
    case SX1262_CMD_GET_PACKET_STATUS:
        assert(bytes == 5 && rx != NULL);
        rx[2] = receive_rssi_raw;
        rx[3] = (uint8_t)receive_snr_raw;
        break;
    case SX1262_CMD_READ_BUFFER:
        assert(bytes == (size_t)receive_length + 3 && rx != NULL);
        assert(tx[1] == receive_offset && tx[2] == 0);
        rx[2] = 0xA5; /* Status/dummy byte must not enter packet data. */
        for (size_t i = 3; i < bytes; ++i) {
            rx[i] = receive_fifo[(uint8_t)(tx[1] + i - 3)];
        }
        break;
    case SX1262_CMD_CLEAR_IRQ_STATUS:
        radio_irq &= (uint16_t)~(((uint16_t)tx[1] << 8) | tx[2]);
        break;
    case SX1262_CMD_SET_RX:
        assert(bytes == 4 && tx[1] == 0xFF && tx[2] == 0xFF && tx[3] == 0xFF);
        assert(gpio_levels[RADIO_PIN_FEM_MODE] == RADIO_FEM_RX_LEVEL);
        radio_mode = 5;
        set_rx_count++;
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void reset_driver(void)
{
    fake_time_us = 0;
    busy_until_us = 0;
    memset(gpio_levels, 0, sizeof(gpio_levels));
    current_task = (void *)1;
    radio_mode = 2;
    radio_irq = 0;
    tx_should_complete = true;
    memset(transmitted, 0, sizeof(transmitted));
    transmitted_length = 0;
    memset(receive_fifo, 0, sizeof(receive_fifo));
    receive_length = receive_offset = 0;
    receive_rssi_raw = 166;
    receive_snr_raw = -8;
    set_tx_count = set_rx_count = 0;
    tcxo_configured = calibration_ran = false;
    s_spi = NULL;
    s_owner = NULL;
    s_status = (carelink_radio_status_t){
        .state = CARELINK_RADIO_STATE_UNINITIALIZED,
        .last_error = ESP_OK,
    };
}

int main(void)
{
    carelink_radio_config_t runtime_radio = {
        .frequency_hz = 915000000U,
        .tx_power_dbm = 0,
    };
    assert(carelink_radio_config_valid(&runtime_radio));
    runtime_radio.frequency_hz = 800000000U;
    assert(!carelink_radio_config_valid(&runtime_radio));
    runtime_radio.frequency_hz = 915000000U;
    runtime_radio.tx_power_dbm = 15;
    assert(!carelink_radio_config_valid(&runtime_radio));

    reset_driver();
    assert(carelink_radio_init() == ESP_OK);
    carelink_radio_status_t status = carelink_radio_get_status();
    assert(status.state == CARELINK_RADIO_STATE_STANDBY);
    assert(tcxo_configured && calibration_ran);
    assert(gpio_levels[RADIO_PIN_FEM_POWER] == 1);
    assert(gpio_levels[RADIO_PIN_FEM_ENABLE] == 1);
    assert(gpio_levels[RADIO_PIN_FEM_MODE] == RADIO_FEM_RX_LEVEL);

    for (size_t length = 1; length <= CARELINK_RADIO_MAX_PAYLOAD_SIZE; ++length) {
        uint8_t payload[CARELINK_RADIO_MAX_PAYLOAD_SIZE];
        for (size_t i = 0; i < length; ++i) payload[i] = (uint8_t)(i ^ length);
        assert(carelink_radio_send(payload, length, CARELINK_RADIO_AFTER_TX_STANDBY) == ESP_OK);
        assert(transmitted_length == length);
        assert(memcmp(transmitted, payload, length) == 0);
        assert(carelink_radio_get_status().state == CARELINK_RADIO_STATE_STANDBY);
    }

    const uint8_t payload[] = {0x00, 0x7F, 0x80, 0xFF};
    assert(carelink_radio_send(payload, sizeof(payload), CARELINK_RADIO_AFTER_TX_RECEIVE) == ESP_OK);
    assert(carelink_radio_get_status().state == CARELINK_RADIO_STATE_RECEIVE_READY);
    assert(set_rx_count == 1);

    carelink_radio_packet_t received = {0};
    bool packet_ready = true;
    const int64_t before_nonblocking = fake_time_us;
    assert(carelink_radio_receive(&received, &packet_ready, 0) == ESP_OK);
    assert(!packet_ready && received.length == 0);
    assert(fake_time_us == before_nonblocking);

    const int64_t before_receive_timeout = fake_time_us;
    packet_ready = true;
    assert(carelink_radio_receive(&received, &packet_ready, 5) == ESP_OK);
    assert(!packet_ready && received.length == 0);
    assert(fake_time_us - before_receive_timeout >= 5000);
    assert(carelink_radio_receive(&received, &packet_ready,
                                  CONFIG_CARELINK_RADIO_RX_MAX_WAIT_MS + 1) ==
           ESP_ERR_INVALID_ARG);

    const uint8_t ack[] = {
        0xCA, 0x01, 0x02, 0x00, 0x7F, 0x80, 0xFF, 0x10,
        0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x00, 0xAA,
    };
    receive_length = sizeof(ack);
    receive_offset = 247; /* Exercise FIFO wrap and the mandatory dummy byte. */
    for (size_t i = 0; i < sizeof(ack); ++i) {
        receive_fifo[(uint8_t)(receive_offset + i)] = ack[i];
    }
    radio_irq = SX1262_IRQ_RX_DONE;
    assert(carelink_radio_receive(&received, &packet_ready, 0) == ESP_OK);
    assert(packet_ready && received.length == sizeof(ack));
    assert(memcmp(received.data, ack, sizeof(ack)) == 0);
    assert(received.rssi_dbm == -83 && received.snr_db == -2);
    assert(carelink_radio_get_status().state == CARELINK_RADIO_STATE_RECEIVE_READY);

    receive_length = CARELINK_RADIO_MAX_PAYLOAD_SIZE;
    receive_offset = 37;
    for (size_t i = 0; i < CARELINK_RADIO_MAX_PAYLOAD_SIZE; ++i) {
        receive_fifo[(uint8_t)(receive_offset + i)] = (uint8_t)(i ^ 0x5A);
    }
    radio_irq = SX1262_IRQ_RX_DONE;
    assert(carelink_radio_receive(&received, &packet_ready, 0) == ESP_OK);
    assert(packet_ready && received.length == CARELINK_RADIO_MAX_PAYLOAD_SIZE);
    for (size_t i = 0; i < CARELINK_RADIO_MAX_PAYLOAD_SIZE; ++i) {
        assert(received.data[i] == (uint8_t)(i ^ 0x5A));
    }

    radio_irq = SX1262_IRQ_CRC_ERROR;
    assert(carelink_radio_receive(&received, &packet_ready, 0) == ESP_ERR_INVALID_CRC);
    assert(!packet_ready && received.length == 0);
    assert(carelink_radio_get_status().state == CARELINK_RADIO_STATE_RECEIVE_READY);

    radio_irq = SX1262_IRQ_HEADER_ERROR;
    assert(carelink_radio_receive(&received, &packet_ready, 0) ==
           ESP_ERR_INVALID_RESPONSE);
    assert(!packet_ready && received.length == 0);

    radio_irq = SX1262_IRQ_TIMEOUT;
    assert(carelink_radio_receive(&received, &packet_ready, 0) == ESP_ERR_TIMEOUT);
    assert(!packet_ready && received.length == 0);

    receive_length = 0;
    radio_irq = SX1262_IRQ_RX_DONE;
    assert(carelink_radio_receive(&received, &packet_ready, 0) ==
           ESP_ERR_INVALID_SIZE);
    assert(!packet_ready && received.length == 0);
    status = carelink_radio_get_status();
    assert(status.received_packets == 2 && status.receive_errors == 4);
    assert(status.state == CARELINK_RADIO_STATE_RECEIVE_READY);

    current_task = (void *)9;
    assert(carelink_radio_receive(&received, &packet_ready, 0) == ESP_ERR_INVALID_STATE);
    current_task = (void *)1;

    assert(carelink_radio_send(NULL, 1, CARELINK_RADIO_AFTER_TX_STANDBY) == ESP_ERR_INVALID_ARG);
    assert(carelink_radio_send(payload, 0, CARELINK_RADIO_AFTER_TX_STANDBY) == ESP_ERR_INVALID_SIZE);
    assert(carelink_radio_send(payload, 256, CARELINK_RADIO_AFTER_TX_STANDBY) == ESP_ERR_INVALID_SIZE);

    current_task = (void *)9;
    assert(carelink_radio_send(payload, sizeof(payload), CARELINK_RADIO_AFTER_TX_STANDBY) ==
           ESP_ERR_INVALID_STATE);
    current_task = (void *)1;

    tx_should_complete = false;
    const int64_t before_timeout = fake_time_us;
    assert(carelink_radio_send(payload, sizeof(payload), CARELINK_RADIO_AFTER_TX_STANDBY) ==
           ESP_ERR_TIMEOUT);
    assert(fake_time_us - before_timeout >=
           (CONFIG_CARELINK_RADIO_TX_TIMEOUT_MS + 100) * 1000LL);
    status = carelink_radio_get_status();
    assert(status.state == CARELINK_RADIO_STATE_STANDBY);
    assert(status.failed_packets == 1);

    busy_until_us = INT64_MAX;
    const unsigned failures_before = status.failed_packets;
    assert(carelink_radio_send(payload, sizeof(payload), CARELINK_RADIO_AFTER_TX_STANDBY) ==
           ESP_ERR_TIMEOUT);
    status = carelink_radio_get_status();
    assert(status.state == CARELINK_RADIO_STATE_FAULT);
    assert(status.failed_packets == failures_before + 1);

    printf("CareLink radio tests passed at %d Hz\n", TEST_TICK_HZ);
    return 0;
}
