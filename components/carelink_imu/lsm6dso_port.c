#include "lsm6dso_port.h"

#include <string.h>
#include "carelink_board.h"
#include "carelink_i2c.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TRANSFER_TIMEOUT_MS 100

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

static int64_t now_ms(void *handle)
{
    (void)handle;
    return esp_timer_get_time() / 1000;
}

static int32_t read_register(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    carelink_lsm6dso_port_t *port = handle;
    port->last_error = i2c_master_transmit_receive(port->device, &reg, 1,
                                                  data, len, TRANSFER_TIMEOUT_MS);
    return port->last_error == ESP_OK ? 0 : -1;
}

static int32_t write_register(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    carelink_lsm6dso_port_t *port = handle;
    /* Current register operations are short; bound stack use and reject larger
     * writes explicitly if future FIFO/embedded-feature support needs them. */
    uint8_t buffer[33];
    if (!data || !len || len > sizeof(buffer) - 1) {
        port->last_error = ESP_ERR_INVALID_ARG;
        return -1;
    }
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    port->last_error = i2c_master_transmit(port->device, buffer, len + 1,
                                          TRANSFER_TIMEOUT_MS);
    return port->last_error == ESP_OK ? 0 : -1;
}

esp_err_t carelink_lsm6dso_port_open(carelink_lsm6dso_port_t *port,
                                    carelink_lsm6dso_t *device, uint8_t address)
{
    if (!port || !device || (address != 0x6a && address != 0x6b)) return ESP_ERR_INVALID_ARG;
    if (port->bus || port->device) return ESP_ERR_INVALID_STATE;
    esp_err_t err = carelink_board_imu_validate();
    if (err != ESP_OK) return err;
    if (carelink_board_imu_uses_vext()) {
        err = carelink_board_vext_power(true);
        if (err != ESP_OK) return err;
        delay_ms(50);
    }
    if (carelink_board_imu_shared_bus()) {
        err = carelink_i2c_get_shared_bus(&port->bus);
    } else {
        const i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = carelink_board_imu_sda_gpio(),
            .scl_io_num = carelink_board_imu_scl_gpio(),
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&config, &port->bus);
        port->owns_bus = err == ESP_OK;
    }
    if (err != ESP_OK) goto fail;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(port->bus, &config, &port->device);
    if (err != ESP_OK) goto fail;
    *device = (carelink_lsm6dso_t) {
        .ctx = {.read_reg = read_register, .write_reg = write_register,
                .mdelay = delay_ms, .handle = port},
        .now_ms = now_ms,
    };
    return ESP_OK;
fail:
    carelink_lsm6dso_port_close(port);
    return err;
}

void carelink_lsm6dso_port_close(carelink_lsm6dso_port_t *port)
{
    if (!port) return;
    if (port->device) i2c_master_bus_rm_device(port->device);
    if (port->owns_bus && port->bus) i2c_del_master_bus(port->bus);
    *port = (carelink_lsm6dso_port_t){0};
    /* Vext is shared. Never power-cycle it during IMU cleanup or recovery. */
}
