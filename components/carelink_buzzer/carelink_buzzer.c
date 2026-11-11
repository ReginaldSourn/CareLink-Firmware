#include "carelink_buzzer.h"

#include "sdkconfig.h"

#if CONFIG_CARELINK_BUZZER_ENABLED

#include <stdint.h>

#include "carelink_board.h"
#include "carelink_buzzer_pattern.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    BUZZER_DUTY_BITS = 10,
    BUZZER_DUTY_MAX = (1U << BUZZER_DUTY_BITS) - 1U,
};

static const char *TAG = "carelink_buzzer";
static StaticTask_t s_task_control;
static StackType_t s_task_stack[
    (CONFIG_CARELINK_BUZZER_TASK_STACK_BYTES + sizeof(StackType_t) - 1U) /
    sizeof(StackType_t)];
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static carelink_buzzer_event_t s_last_event = CARELINK_BUZZER_EVENT_COUNT;
static bool s_started;

static esp_err_t tone(bool enabled)
{
    const uint32_t duty = enabled
                              ? (BUZZER_DUTY_MAX *
                                 CONFIG_CARELINK_BUZZER_DUTY_PERCENT) /
                                    100U
                              : 0U;
    esp_err_t error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                    duty);
    if (error == ESP_OK) {
        error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    return error;
}

static bool wait_event(TickType_t timeout, carelink_buzzer_event_t *event)
{
    uint32_t notification = 0U;
    if (xTaskNotifyWait(0U, UINT32_MAX, &notification, timeout) != pdTRUE) {
        return false;
    }
    if (notification == 0U || notification > CARELINK_BUZZER_EVENT_COUNT) {
        return false;
    }
    *event = (carelink_buzzer_event_t)(notification - 1U);
    return true;
}

static void buzzer_task(void *context)
{
    (void)context;
    carelink_buzzer_event_t event = CARELINK_BUZZER_SILENT;
    (void)tone(false);
    for (;;) {
        if (!wait_event(portMAX_DELAY, &event)) {
            continue;
        }
        for (;;) {
            const carelink_buzzer_pattern_t pattern =
                carelink_buzzer_pattern(event);
            if (pattern.count == 0U) {
                (void)tone(false);
                break;
            }

            bool changed = false;
            bool output_failed = false;
            for (size_t index = 0U; index < pattern.count; ++index) {
                if (tone(pattern.segments[index].tone) != ESP_OK) {
                    ESP_LOGE(TAG, "PWM update failed; silencing buzzer");
                    (void)tone(false);
                    output_failed = true;
                    break;
                }
                const TickType_t duration =
                    pdMS_TO_TICKS(pattern.segments[index].duration_ms);
                carelink_buzzer_event_t next_event;
                if (wait_event(duration > 0U ? duration : 1U, &next_event)) {
                    event = next_event;
                    changed = true;
                    break;
                }
            }
            if (changed) {
                continue;
            }
            (void)tone(false);
            if (output_failed || !pattern.repeat) {
                break;
            }
        }
    }
}

static esp_err_t validate_config(void)
{
    return carelink_buzzer_pin_is_safe(
               CONFIG_CARELINK_BUZZER_GPIO,
               carelink_board_gps_rx_gpio(),
               carelink_board_imu_sda_gpio(),
               carelink_board_imu_scl_gpio(),
               CONFIG_CARELINK_BUTTON_GPIO)
               ? ESP_OK
               : ESP_ERR_INVALID_ARG;
}

esp_err_t carelink_buzzer_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = validate_config();
    if (error != ESP_OK) {
        return error;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = CONFIG_CARELINK_BUZZER_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    error = ledc_timer_config(&timer);
    if (error != ESP_OK) {
        return error;
    }
    const ledc_channel_config_t channel = {
        .gpio_num = CONFIG_CARELINK_BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0U,
        .hpoint = 0U,
    };
    error = ledc_channel_config(&channel);
    if (error != ESP_OK) {
        (void)ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        return error;
    }

    s_task = xTaskCreateStatic(
        buzzer_task, "carelink_buzzer",
        sizeof(s_task_stack) / sizeof(s_task_stack[0]), NULL,
        CONFIG_CARELINK_BUZZER_TASK_PRIORITY, s_task_stack, &s_task_control);
    if (s_task == NULL) {
        (void)ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0U);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_lock);
    s_started = true;
    s_last_event = CARELINK_BUZZER_EVENT_COUNT;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "passive buzzer ready on GPIO%d at %d Hz",
             CONFIG_CARELINK_BUZZER_GPIO,
             CONFIG_CARELINK_BUZZER_FREQUENCY_HZ);
    return ESP_OK;
}

esp_err_t carelink_buzzer_notify_sos(carelink_sos_state_t state,
                                     bool cancellation_pending)
{
    const carelink_buzzer_event_t event =
        carelink_buzzer_event_for_sos((int)state, cancellation_pending);
    portENTER_CRITICAL(&s_lock);
    if (!s_started || s_task == NULL) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (event == s_last_event) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    s_last_event = event;
    const TaskHandle_t task = s_task;
    portEXIT_CRITICAL(&s_lock);
    return xTaskNotify(task, (uint32_t)event + 1U, eSetValueWithOverwrite) ==
                   pdPASS
               ? ESP_OK
               : ESP_FAIL;
}

#else

esp_err_t carelink_buzzer_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_buzzer_notify_sos(carelink_sos_state_t state,
                                     bool cancellation_pending)
{
    (void)state;
    (void)cancellation_pending;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
