#pragma once

#include <stddef.h>
#include <stdint.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_FRAME_BYTES (OLED_WIDTH * OLED_HEIGHT / 8)

/* SSD1306 page order: each byte represents eight vertical pixels. */
void oled_text_render(uint8_t frame[OLED_FRAME_BYTES],
                      const char *const lines[], size_t count);
