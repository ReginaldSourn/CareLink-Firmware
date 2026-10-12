#include "oled_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct {
        uint8_t before[16];
        uint8_t pixels[OLED_FRAME_BYTES];
        uint8_t after[16];
    } guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    const char *rows[] = {
        "CARELINK GPS", "FIX", "LAT -12.345678", "LON +104.123456",
        "ALT 10.0 M", "SAT 12 HDOP 0.9", "AGE 0.1 S", "UART ERR 0",
        "THIS ROW MUST BE CLIPPED",
    };
    oled_text_render(guarded.pixels, rows, 9);
    /* Optional raw framebuffer for visual inspection. */
    if (argc == 2) {
        FILE *out = fopen(argv[1], "wb");
        assert(out);
        assert(fwrite(guarded.pixels, 1, OLED_FRAME_BYTES, out) == OLED_FRAME_BYTES);
        assert(fclose(out) == 0);
    }
    for (size_t i = 0; i < 16; ++i) {
        assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
    }
    /* Changing to a no-fix screen must erase all old coordinate rows. */
    const char *status[] = {"CARELINK GPS", "STALE FIX"};
    oled_text_render(guarded.pixels, status, 2);
    for (size_t i = 2 * OLED_WIDTH; i < OLED_FRAME_BYTES; ++i) assert(guarded.pixels[i] == 0);

    const char *clipped[] = {"012345678901234567890EXTRA CHARACTERS", NULL, "abc", "ABC", "?", "@"};
    oled_text_render(guarded.pixels, clipped, 6);
    for (size_t i = OLED_WIDTH; i < 2 * OLED_WIDTH; ++i) assert(guarded.pixels[i] == 0);
    assert(guarded.pixels[126] == 0 && guarded.pixels[127] == 0);
    assert(memcmp(guarded.pixels + 2 * OLED_WIDTH, guarded.pixels + 3 * OLED_WIDTH, OLED_WIDTH) == 0);
    assert(memcmp(guarded.pixels + 4 * OLED_WIDTH, guarded.pixels + 5 * OLED_WIDTH, OLED_WIDTH) == 0);
    oled_text_render(guarded.pixels, NULL, 0);
    for (size_t i = 0; i < OLED_FRAME_BYTES; ++i) assert(guarded.pixels[i] == 0);
    puts("OLED clipping, page layout and stale-screen clearing passed");
    return 0;
}
