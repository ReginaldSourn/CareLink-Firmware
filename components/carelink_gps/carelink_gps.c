#include "carelink_gps.h"

#include <inttypes.h>
#include <string.h>

#include "carelink_board.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define GPS_UART UART_NUM_1
#define RX_BUFFER_BYTES 2048

static const char *TAG = "gps";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static carelink_gps_snapshot_t shared;
static QueueHandle_t events;
static bool started;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void gps_task(void *arg)
{
    (void)arg;
    carelink_nmea_t parser = {0};
    carelink_gps_snapshot_t current = {
        .last_byte_ms = -1, .last_sentence_ms = -1, .fix_report_ms = -1,
    };
    uint8_t data[256];
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
            event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR ||
            event.type == UART_BREAK) {
            ++current.uart_errors;
            uart_flush_input(GPS_UART);
            xQueueReset(events);
            memset(&parser, 0, sizeof(parser));
            /* An incomplete stream must not preserve a current fix. */
            current.fix.valid = false;
            current.last_sentence_ms = -1;
        } else if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining) {
                size_t chunk = remaining < sizeof(data) ? remaining : sizeof(data);
                int n = uart_read_bytes(GPS_UART, data, chunk, 0);
                if (n <= 0) break;
                remaining -= (size_t)n;
                current.bytes_received += (uint32_t)n;
                current.last_byte_ms = now_ms();
                for (int i = 0; i < n; ++i) {
#if CONFIG_CARELINK_GPS_RAW_LOG
                    if ((data[i] == '\r' || data[i] == '\n') && parser.collecting) {
                        ESP_LOGI(TAG, "NMEA: %.*s", (int)parser.length, parser.line);
                    }
#endif
                    carelink_fix_t fix;
                    carelink_nmea_result_t result = carelink_nmea_feed(&parser, data[i], &fix);
                    if (result == CARELINK_NMEA_FIX || result == CARELINK_NMEA_OTHER) {
                        ++current.accepted_sentences;
                        current.last_sentence_ms = now_ms();
                        if (result == CARELINK_NMEA_FIX) {
                            current.fix = fix;
                            current.fix_report_ms = current.last_sentence_ms;
                        }
                    } else if (result == CARELINK_NMEA_REJECTED) {
                        ++current.rejected_sentences;
                    }
                }
            }
        }
        taskENTER_CRITICAL(&lock);
        shared = current;
        taskEXIT_CRITICAL(&lock);
    }
}

esp_err_t carelink_gps_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    esp_err_t err = carelink_board_validate();
    if (err != ESP_OK) return err;
    const uart_config_t config = {
        .baud_rate = carelink_board_gps_baud(),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_driver_install(GPS_UART, RX_BUFFER_BYTES, 0, 20, &events, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(GPS_UART, &config);
    if (err != ESP_OK) goto fail;
    /* RX only: do not drive a potentially incompatible module I/O voltage. */
    err = uart_set_pin(GPS_UART, UART_PIN_NO_CHANGE, carelink_board_gps_rx_gpio(),
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) goto fail;
    err = carelink_board_gnss_power(true);
    if (err != ESP_OK) goto fail;
    shared = (carelink_gps_snapshot_t) {
        .last_byte_ms = -1, .last_sentence_ms = -1, .fix_report_ms = -1,
    };
    if (xTaskCreate(gps_task, "carelink_gps", 4096, NULL, 5, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    started = true;
    ESP_LOGI(TAG, "UART1 RX=GPIO%d, %d baud, 8N1 (receive only)",
             carelink_board_gps_rx_gpio(), carelink_board_gps_baud());
    return ESP_OK;
fail:
    carelink_board_gnss_power(false);
    uart_driver_delete(GPS_UART);
    events = NULL;
    return err;
}

esp_err_t carelink_gps_get_snapshot(carelink_gps_snapshot_t *snapshot)
{
    if (!snapshot) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&lock);
    *snapshot = shared;
    taskEXIT_CRITICAL(&lock);
    snapshot->state = carelink_gps_classify(snapshot, now_ms(), CONFIG_CARELINK_GPS_STALE_MS);
    return ESP_OK;
}
