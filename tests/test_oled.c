#include "carelink_display_format.h"
#include "oled_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_line(
    char lines[CARELINK_DISPLAY_ROWS][CARELINK_DISPLAY_COLUMNS + 1U],
    size_t row,
    const char *expected)
{
    assert(row < CARELINK_DISPLAY_ROWS);
    assert(strcmp(lines[row], expected) == 0);
}

static void test_user_status_format(void)
{
    char lines[CARELINK_DISPLAY_ROWS][CARELINK_DISPLAY_COLUMNS + 1U];
    const carelink_display_view_t ready = {
        .sos = CARELINK_DISPLAY_SOS_READY,
        .gps = CARELINK_DISPLAY_GPS_READY,
        .battery_available = true,
        .battery_mv = 4021U,
    };
    carelink_display_format(&ready, lines);
    assert_line(lines, 0U, "CARELINK");
    assert_line(lines, 1U, "STATUS READY");
    assert_line(lines, 2U, "SOS READY");
    assert_line(lines, 3U, "");
    assert_line(lines, 4U, "GPS READY");
    assert_line(lines, 5U, "BATTERY 4.02 V");
    assert_line(lines, 6U, "");
    assert_line(lines, 7U, "HOLD SOS FOR HELP");

    const carelink_display_view_t sending = {
        .sos = CARELINK_DISPLAY_SOS_SENDING,
        .gps = CARELINK_DISPLAY_GPS_SEARCHING,
        .battery_available = false,
    };
    carelink_display_format(&sending, lines);
    assert_line(lines, 1U, "SOS ACTIVE");
    assert_line(lines, 2U, "CONTACTING GATEWAY");
    assert_line(lines, 4U, "GPS SEARCHING");
    assert_line(lines, 5U, "BATTERY N/A");
    assert_line(lines, 7U, "DO NOT POWER OFF");

    const carelink_display_view_t cancelling = {
        .sos = CARELINK_DISPLAY_SOS_CANCELLING,
        .gps = CARELINK_DISPLAY_GPS_STALE,
        .battery_available = true,
        .battery_mv = 3999U,
    };
    carelink_display_format(&cancelling, lines);
    assert_line(lines, 1U, "SOS CANCELLED");
    assert_line(lines, 2U, "SENDING CANCELLATION");
    assert_line(lines, 4U, "GPS STALE");
    assert_line(lines, 5U, "BATTERY 3.99 V");
    assert_line(lines, 7U, "DO NOT POWER OFF");

    carelink_display_format(NULL, lines);
    assert_line(lines, 0U, "CARELINK");
    assert_line(lines, 1U, "STARTING");
    assert_line(lines, 7U, "PLEASE WAIT");
}

int main(int argc, char **argv)
{
    test_user_status_format();

    struct {
        uint8_t before[16];
        uint8_t pixels[OLED_FRAME_BYTES];
        uint8_t after[16];
    } guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    const char *rows[] = {
        "CARELINK", "SOS ACTIVE", "CONTACTING GATEWAY", "",
        "GPS SEARCHING", "BATTERY 4.02 V", "", "DO NOT POWER OFF",
        "THIS ROW MUST BE CLIPPED",
    };
    oled_text_render(guarded.pixels, rows, 9U);
    /* Optional raw framebuffer for visual inspection. */
    if (argc == 2) {
        FILE *out = fopen(argv[1], "wb");
        assert(out != NULL);
        assert(fwrite(guarded.pixels, 1U, OLED_FRAME_BYTES, out) ==
               OLED_FRAME_BYTES);
        assert(fclose(out) == 0);
    }
    for (size_t i = 0; i < 16U; ++i) {
        assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
    }
    /* A shorter status update must erase every unused row. */
    const char *status[] = {"CARELINK", "STARTING"};
    oled_text_render(guarded.pixels, status, 2U);
    for (size_t i = 2U * OLED_WIDTH; i < OLED_FRAME_BYTES; ++i) {
        assert(guarded.pixels[i] == 0U);
    }

    const char *clipped[] = {
        "012345678901234567890EXTRA CHARACTERS", NULL, "abc", "ABC", "?", "@",
    };
    oled_text_render(guarded.pixels, clipped, 6U);
    for (size_t i = OLED_WIDTH; i < 2U * OLED_WIDTH; ++i) {
        assert(guarded.pixels[i] == 0U);
    }
    assert(guarded.pixels[126] == 0U && guarded.pixels[127] == 0U);
    assert(memcmp(guarded.pixels + 2U * OLED_WIDTH,
                  guarded.pixels + 3U * OLED_WIDTH,
                  OLED_WIDTH) == 0);
    assert(memcmp(guarded.pixels + 4U * OLED_WIDTH,
                  guarded.pixels + 5U * OLED_WIDTH,
                  OLED_WIDTH) == 0);
    oled_text_render(guarded.pixels, NULL, 0U);
    for (size_t i = 0; i < OLED_FRAME_BYTES; ++i) {
        assert(guarded.pixels[i] == 0U);
    }
    puts("OLED user status, clipping and stale-screen clearing passed");
    return 0;
}
