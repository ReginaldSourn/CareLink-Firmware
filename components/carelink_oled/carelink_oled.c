#include "carelink_oled.h"
#include "sdkconfig.h"

#if CONFIG_CARELINK_OLED_ENABLED
#include "carelink_board.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_text.h"

static i2c_master_bus_handle_t bus;
static esp_lcd_panel_io_handle_t io;
static esp_lcd_panel_handle_t panel;
static uint8_t frame[OLED_FRAME_BYTES];

static void release_resources(void)
{
    if (panel) { esp_lcd_panel_del(panel); panel = NULL; }
    if (io) { esp_lcd_panel_io_del(io); io = NULL; }
    if (bus) { i2c_del_master_bus(bus); bus = NULL; }
    /* Do not switch off Vext: it also supplies external header devices. */
}

esp_err_t carelink_oled_init(void)
{
    if (panel) return ESP_ERR_INVALID_STATE;
    esp_err_t err = carelink_board_vext_power(true);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CARELINK_OLED_SDA_GPIO,
        .scl_io_num = CARELINK_OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) goto fail;
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = CARELINK_OLED_I2C_ADDRESS,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .transaction_timeout_ms = 100,
    };
    err = esp_lcd_new_panel_io_i2c(bus, &io_config, &io);
    if (err != ESP_OK) goto fail;
    esp_lcd_panel_ssd1306_config_t vendor = {.height = OLED_HEIGHT, .contrast = 0xcf};
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CARELINK_OLED_RESET_GPIO,
        .bits_per_pixel = 1,
        .vendor_config = &vendor,
    };
    err = esp_lcd_new_panel_ssd1306(io, &panel_config, &panel);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_reset(panel);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) goto fail;
    /* SSD1315 uses this SSD1306 command set. Match Heltec's panel timing. */
    const uint8_t clock_div = 0xf0, precharge = 0xf1, vcom = 0x40;
    err = esp_lcd_panel_io_tx_param(io, 0xd5, &clock_div, 1);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_io_tx_param(io, 0xd9, &precharge, 1);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_io_tx_param(io, 0xdb, &vcom, 1);
    if (err != ESP_OK) goto fail;
#if CONFIG_CARELINK_OLED_ROTATE_180
    err = esp_lcd_panel_mirror(panel, false, false);
#else
    err = esp_lcd_panel_mirror(panel, true, true);
#endif
    if (err != ESP_OK) goto fail;
    /* Clear display RAM before enabling pixels. */
    oled_text_render(frame, NULL, 0);
    err = esp_lcd_panel_draw_bitmap(panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, frame);
    if (err != ESP_OK) goto fail;
    err = esp_lcd_panel_disp_on_off(panel, true);
    if (err != ESP_OK) goto fail;
    return ESP_OK;
fail:
    release_resources();
    return err;
}

esp_err_t carelink_oled_show_lines(const char *const lines[], size_t count)
{
    if (!panel) return ESP_ERR_INVALID_STATE;
    if (!lines && count) return ESP_ERR_INVALID_ARG;
    oled_text_render(frame, lines, count);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, frame);
    if (err != ESP_OK) {
        /* Hardware reset switches the panel off without relying on I2C. Avoid
         * leaving an old FIX visible after a broken transfer. */
        esp_lcd_panel_reset(panel);
        release_resources();
    }
    return err;
}
#else
esp_err_t carelink_oled_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t carelink_oled_show_lines(const char *const lines[], size_t count)
{
    (void)lines;
    (void)count;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
