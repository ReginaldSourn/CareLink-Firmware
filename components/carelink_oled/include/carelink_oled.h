#pragma once

#include <stddef.h>
#include "esp_err.h"

#define CARELINK_OLED_ROWS 8
#define CARELINK_OLED_COLUMNS 21

/* Single caller (app_main). Owns I2C0. ESP_ERR_NOT_SUPPORTED when disabled.
 * Failed initialization releases the bus/panel and may be retried. */
esp_err_t carelink_oled_init(void);

/* Replaces the screen with up to 8 rows of 21 characters. Lowercase is shown
 * as uppercase; unsupported glyphs become '?'. Empty rows are cleared.
 * Synchronous, bounded transfers; no per-frame heap allocation. */
esp_err_t carelink_oled_show_lines(const char *const lines[], size_t count);
