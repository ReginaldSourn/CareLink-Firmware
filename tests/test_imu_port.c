#include <assert.h>
#include <string.h>
#include "lsm6dso_port.h"
#include "carelink_board.h"
#include "carelink_i2c.h"
#include "freertos/task.h"

struct fake_bus { int port; } bus0 = {0}, bus1 = {1};
struct fake_device { int active; } sensor;
static bool shared_mode;
static bool valid_pins = true;
static int power_calls, bus_creates, bus_deletes, device_deletes, transactions;
static esp_err_t power_error, create_error, add_error, transfer_error;
static int64_t time_us;

esp_err_t carelink_board_imu_validate(void) { return valid_pins ? ESP_OK : ESP_ERR_INVALID_ARG; }
bool carelink_board_imu_uses_vext(void) { return true; }
bool carelink_board_imu_shared_bus(void) { return shared_mode; }
int carelink_board_imu_sda_gpio(void) { return 4; }
int carelink_board_imu_scl_gpio(void) { return 6; }
esp_err_t carelink_board_vext_power(bool enabled)
{
    assert(enabled); /* Shared supply must never be switched off. */
    ++power_calls;
    return power_error;
}
void vTaskDelay(TickType_t ticks) { time_us += (int64_t)ticks * 10000; }
int64_t esp_timer_get_time(void) { return time_us; }

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg, i2c_master_bus_handle_t *bus)
{
    ++bus_creates;
    if (create_error) return create_error;
    if (cfg->i2c_port == I2C_NUM_0) {
        assert(cfg->sda_io_num == 17 && cfg->scl_io_num == 18);
        *bus = &bus0;
    } else {
        assert(cfg->i2c_port == I2C_NUM_1);
        assert(cfg->sda_io_num == 4 && cfg->scl_io_num == 6);
        *bus = &bus1;
    }
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
    assert(bus == &bus1); /* Never delete shared bus. */
    assert(!sensor.active);
    ++bus_deletes;
    return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                   const i2c_device_config_t *cfg, i2c_master_dev_handle_t *dev)
{
    assert(bus == (shared_mode ? &bus0 : &bus1));
    assert(cfg->device_address == 0x6a || cfg->device_address == 0x6b);
    assert(cfg->scl_speed_hz == 400000);
    assert(cfg->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    if (add_error) return add_error;
    assert(!sensor.active);
    sensor.active = 1;
    *dev = &sensor;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    assert(dev == &sensor && sensor.active);
    sensor.active = 0;
    ++device_deletes;
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *tx,
                                     size_t tx_len, uint8_t *rx, size_t rx_len, int timeout)
{
    assert(dev == &sensor && sensor.active);
    assert(tx_len == 1 && tx[0] == LSM6DSO_WHO_AM_I && rx_len == 1);
    assert(timeout == 100);
    ++transactions;
    rx[0] = LSM6DSO_ID;
    return transfer_error;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *tx, size_t len, int timeout)
{
    assert(dev == &sensor && sensor.active);
    assert(timeout == 100 && len == 2 && tx[0] == LSM6DSO_CTRL1_XL && tx[1] == 0x40);
    ++transactions;
    return transfer_error;
}

int main(void)
{
    carelink_lsm6dso_port_t port = {0};
    carelink_lsm6dso_t device = {0};
    assert(carelink_lsm6dso_port_open(&port, &device, 0xd4) == ESP_ERR_INVALID_ARG);
    valid_pins = false;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6a) == ESP_ERR_INVALID_ARG);
    assert(power_calls == 0 && bus_creates == 0);
    valid_pins = true;
    power_error = ESP_FAIL;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6a) == ESP_FAIL);
    assert(bus_creates == 0);
    power_error = ESP_OK;
    create_error = ESP_FAIL;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6a) == ESP_FAIL);
    assert(!port.bus && !port.device && !port.owns_bus && bus_deletes == 0);
    create_error = ESP_OK;
    add_error = ESP_FAIL;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6a) == ESP_FAIL);
    assert(!port.bus && !port.device && !port.owns_bus && bus_deletes == 1);
    add_error = ESP_OK;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6b) == ESP_OK);
    assert(time_us >= 50000 && port.owns_bus);
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6b) == ESP_ERR_INVALID_STATE);
    uint8_t value = 0;
    assert(device.ctx.read_reg(device.ctx.handle, LSM6DSO_WHO_AM_I, &value, 1) == 0);
    assert(value == LSM6DSO_ID);
    value = 0x40;
    assert(device.ctx.write_reg(device.ctx.handle, LSM6DSO_CTRL1_XL, &value, 1) == 0);
    transfer_error = ESP_ERR_TIMEOUT;
    assert(device.ctx.read_reg(device.ctx.handle, LSM6DSO_WHO_AM_I, &value, 1) != 0);
    assert(port.last_error == ESP_ERR_TIMEOUT);
    uint8_t oversized[33] = {0};
    int before = transactions;
    assert(device.ctx.write_reg(device.ctx.handle, 0, oversized, sizeof(oversized)) != 0);
    assert(port.last_error == ESP_ERR_INVALID_ARG && transactions == before);
    carelink_lsm6dso_port_close(&port);
    assert(bus_deletes == 2 && device_deletes == 1);
    carelink_lsm6dso_port_close(&port);
    assert(bus_deletes == 2 && device_deletes == 1);

    shared_mode = true;
    add_error = ESP_FAIL;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6a) == ESP_FAIL);
    assert(!port.bus && bus_deletes == 2);
    before = bus_creates;
    add_error = ESP_OK;
    assert(carelink_lsm6dso_port_open(&port, &device, 0x6a) == ESP_OK);
    assert(bus_creates == before && !port.owns_bus);
    carelink_lsm6dso_port_close(&port);
    assert(bus_deletes == 2 && device_deletes == 2);
    i2c_master_bus_handle_t retained = NULL;
    assert(carelink_i2c_get_shared_bus(&retained) == ESP_OK && retained == &bus0);
    assert(bus_creates == before);
    return 0;
}
