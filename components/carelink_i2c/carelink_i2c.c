#include "carelink_i2c.h"
#include "carelink_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_bus_handle_t shared_bus;

esp_err_t carelink_i2c_get_shared_bus(i2c_master_bus_handle_t *bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    *bus = NULL;
    if (!shared_bus) {
        esp_err_t err = carelink_board_vext_power(true);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(50) + 1);
        const i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = CARELINK_OLED_SDA_GPIO,
            .scl_io_num = CARELINK_OLED_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&config, &shared_bus);
        if (err != ESP_OK) return err;
    }
    *bus = shared_bus;
    return ESP_OK;
}
