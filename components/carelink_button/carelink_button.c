#include "carelink_button.h"

#include "sdkconfig.h"

#if CONFIG_CARELINK_BUTTON_ENABLED

#include <inttypes.h>
#include <limits.h>

#include "carelink_board.h"
#include "carelink_button_logic.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUTTON_EVENT_QUEUE_LENGTH 8U
#define BUTTON_POLL_MS 10U
#define BUTTON_STACK_WORDS \
    (CONFIG_CARELINK_BUTTON_TASK_STACK_BYTES / sizeof(StackType_t))

static const char *TAG = "carelink_button";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static carelink_button_logic_t s_logic;
static carelink_button_stats_t s_stats;
static carelink_button_event_callback_t s_callback;
static void *s_callback_context;
static QueueHandle_t s_event_queue;
static StaticQueue_t s_event_queue_control;
static uint8_t s_event_queue_storage[BUTTON_EVENT_QUEUE_LENGTH *
                                     sizeof(carelink_button_event_t)];
static TaskHandle_t s_task;
static StaticTask_t s_task_control;
static StackType_t s_task_stack[BUTTON_STACK_WORDS];
static bool s_started;
static bool s_handler_added;
static uint32_t s_sequence;

static uint32_t now_ms(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() / 1000U);
}

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static bool gpio_active(void)
{
    int level = gpio_get_level((gpio_num_t)CONFIG_CARELINK_BUTTON_GPIO);
#if CONFIG_CARELINK_BUTTON_ACTIVE_LOW
    return level == 0;
#else
    return level != 0;
#endif
}

static void publish_event(carelink_button_event_type_t type, uint32_t timestamp_ms,
                          uint32_t held_ms)
{
    carelink_button_event_callback_t callback;
    void *callback_context;
    carelink_button_event_t event = {
        .type = type,
        .timestamp_ms = timestamp_ms,
        .held_ms = held_ms,
        .sequence = ++s_sequence,
    };

    taskENTER_CRITICAL(&s_lock);
    ++s_stats.published_events;
    if (type == CARELINK_BUTTON_EVENT_PRESSED ||
        type == CARELINK_BUTTON_EVENT_CALL_REQUESTED) {
        s_stats.pressed = true;
    }
    if (type == CARELINK_BUTTON_EVENT_CALL_REQUESTED) {
        s_stats.call_requested = true;
    }
    if (type == CARELINK_BUTTON_EVENT_SOS_TRIGGERED) s_stats.sos_triggered = true;
    if (type == CARELINK_BUTTON_EVENT_STUCK) s_stats.stuck = true;
    if (type == CARELINK_BUTTON_EVENT_RELEASED) {
        s_stats.pressed = false;
        s_stats.call_requested = false;
        s_stats.sos_triggered = false;
        s_stats.stuck = false;
    }
    event.dropped_events = s_stats.dropped_events;
    callback = s_callback;
    callback_context = s_callback_context;
    taskEXIT_CRITICAL(&s_lock);

    switch (type) {
    case CARELINK_BUTTON_EVENT_PRESSED:
        ESP_LOGI(TAG,
                 "BUTTON pressed=yes GPIO%d sequence=%" PRIu32,
                 CONFIG_CARELINK_BUTTON_GPIO, event.sequence);
        break;
    case CARELINK_BUTTON_EVENT_RELEASED:
        ESP_LOGI(TAG,
                 "BUTTON pressed=no GPIO%d held=%" PRIu32
                 " ms sequence=%" PRIu32,
                 CONFIG_CARELINK_BUTTON_GPIO, held_ms, event.sequence);
        break;
    case CARELINK_BUTTON_EVENT_CALL_REQUESTED:
        ESP_LOGI(TAG,
                 "BUTTON double-press GPIO%d: caregiver call requested "
                 "sequence=%" PRIu32,
                 CONFIG_CARELINK_BUTTON_GPIO, event.sequence);
        break;
    case CARELINK_BUTTON_EVENT_SOS_TRIGGERED:
        ESP_LOGI(TAG,
                 "BUTTON hold GPIO%d held=%" PRIu32
                 " ms: SOS triggered sequence=%" PRIu32,
                 CONFIG_CARELINK_BUTTON_GPIO, held_ms, event.sequence);
        break;
    case CARELINK_BUTTON_EVENT_STUCK:
        ESP_LOGW(TAG,
                 "BUTTON stuck GPIO%d pressed=yes held=%" PRIu32
                 " ms sequence=%" PRIu32,
                 CONFIG_CARELINK_BUTTON_GPIO, held_ms, event.sequence);
        break;
    default:
        break;
    }

    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        carelink_button_event_t discarded;
        (void)xQueueReceive(s_event_queue, &discarded, 0);
        taskENTER_CRITICAL(&s_lock);
        ++s_stats.dropped_events;
        event.dropped_events = s_stats.dropped_events;
        taskEXIT_CRITICAL(&s_lock);
        (void)xQueueSend(s_event_queue, &event, 0);
    }
    if (callback != NULL) callback(&event, callback_context);
}

static void process_sample(uint32_t timestamp_ms)
{
    carelink_button_logic_result_t result =
        carelink_button_logic_sample(&s_logic, gpio_active(), timestamp_ms);
    if ((result.flags & CARELINK_BUTTON_LOGIC_CALL_REQUESTED) != 0U) {
        publish_event(CARELINK_BUTTON_EVENT_CALL_REQUESTED, timestamp_ms,
                      result.held_ms);
    } else if ((result.flags & CARELINK_BUTTON_LOGIC_PRESSED) != 0U) {
        publish_event(CARELINK_BUTTON_EVENT_PRESSED, timestamp_ms, result.held_ms);
    }
    if ((result.flags & CARELINK_BUTTON_LOGIC_SOS_TRIGGERED) != 0U) {
        publish_event(CARELINK_BUTTON_EVENT_SOS_TRIGGERED, timestamp_ms, result.held_ms);
    }
    if ((result.flags & CARELINK_BUTTON_LOGIC_STUCK) != 0U) {
        publish_event(CARELINK_BUTTON_EVENT_STUCK, timestamp_ms, result.held_ms);
    }
    if ((result.flags & CARELINK_BUTTON_LOGIC_RELEASED) != 0U) {
        publish_event(CARELINK_BUTTON_EVENT_RELEASED, timestamp_ms, result.held_ms);
    }
}

static void IRAM_ATTR button_gpio_isr(void *context)
{
    (void)context;
    uint32_t edge_tick = (uint32_t)xTaskGetTickCountFromISR();
    BaseType_t higher_priority_woken = pdFALSE;
    (void)xTaskNotifyFromISR(s_task, edge_tick, eSetValueWithOverwrite,
                            &higher_priority_woken);
    if (higher_priority_woken == pdTRUE) portYIELD_FROM_ISR();
}

static void button_task(void *context)
{
    (void)context;
    for (;;) {
        uint32_t edge_tick = 0;
        BaseType_t edge_seen = xTaskNotifyWait(0, UINT32_MAX, &edge_tick,
                                              pdMS_TO_TICKS(BUTTON_POLL_MS));
        if (edge_seen == pdTRUE) {
            taskENTER_CRITICAL(&s_lock);
            s_stats.last_edge_timestamp_ms = ticks_to_ms((TickType_t)edge_tick);
            taskEXIT_CRITICAL(&s_lock);
        }
        process_sample(now_ms());
    }
}

static esp_err_t validate_config(void)
{
    const int button_gpio = CONFIG_CARELINK_BUTTON_GPIO;
    carelink_button_logic_config_t config = {
        .debounce_ms = CONFIG_CARELINK_BUTTON_DEBOUNCE_MS,
        .double_press_ms = CONFIG_CARELINK_BUTTON_DOUBLE_PRESS_MS,
        .hold_ms = CONFIG_CARELINK_BUTTON_HOLD_MS,
        .stuck_ms = CONFIG_CARELINK_BUTTON_STUCK_MS,
    };
    if (!carelink_button_logic_config_valid(&config)) return ESP_ERR_INVALID_ARG;
#if CONFIG_CARELINK_BUTTON_ACTIVE_LOW && CONFIG_CARELINK_BUTTON_PULL_DOWN
    return ESP_ERR_INVALID_ARG;
#elif CONFIG_CARELINK_BUTTON_ACTIVE_HIGH && CONFIG_CARELINK_BUTTON_PULL_UP
    return ESP_ERR_INVALID_ARG;
#endif
    if (!carelink_button_pin_is_safe(button_gpio, carelink_board_gps_rx_gpio())) {
        return ESP_ERR_INVALID_ARG;
    }
    if (button_gpio == carelink_board_imu_sda_gpio() ||
        button_gpio == carelink_board_imu_scl_gpio()) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t carelink_button_start(void)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    esp_err_t error = validate_config();
    if (error != ESP_OK) return error;

    gpio_config_t input_config = {
        .pin_bit_mask = UINT64_C(1) << CONFIG_CARELINK_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en =
#if CONFIG_CARELINK_BUTTON_PULL_UP
            GPIO_PULLUP_ENABLE,
#else
            GPIO_PULLUP_DISABLE,
#endif
        .pull_down_en =
#if CONFIG_CARELINK_BUTTON_PULL_DOWN
            GPIO_PULLDOWN_ENABLE,
#else
            GPIO_PULLDOWN_DISABLE,
#endif
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&input_config);
    if (error != ESP_OK) return error;

    carelink_button_logic_config_t logic_config = {
        .debounce_ms = CONFIG_CARELINK_BUTTON_DEBOUNCE_MS,
        .double_press_ms = CONFIG_CARELINK_BUTTON_DOUBLE_PRESS_MS,
        .hold_ms = CONFIG_CARELINK_BUTTON_HOLD_MS,
        .stuck_ms = CONFIG_CARELINK_BUTTON_STUCK_MS,
    };
    uint32_t started_ms = now_ms();
    if (!carelink_button_logic_init(&s_logic, &logic_config, gpio_active(), started_ms)) {
        error = ESP_ERR_INVALID_ARG;
        goto fail_gpio;
    }

    s_stats = (carelink_button_stats_t) {
        .last_edge_timestamp_ms = started_ms,
        .pressed = s_logic.stable_active,
    };
    s_sequence = 0;
    s_event_queue = xQueueCreateStatic(BUTTON_EVENT_QUEUE_LENGTH,
                                       sizeof(carelink_button_event_t),
                                       s_event_queue_storage, &s_event_queue_control);
    if (s_event_queue == NULL) {
        error = ESP_ERR_NO_MEM;
        goto fail_gpio;
    }
    s_task = xTaskCreateStatic(button_task, "carelink_sos_button",
                               CONFIG_CARELINK_BUTTON_TASK_STACK_BYTES, NULL, 6,
                               s_task_stack, &s_task_control);
    if (s_task == NULL) {
        error = ESP_ERR_NO_MEM;
        goto fail_queue;
    }

    error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) goto fail_task;
    error = gpio_isr_handler_add((gpio_num_t)CONFIG_CARELINK_BUTTON_GPIO,
                                 button_gpio_isr, NULL);
    if (error != ESP_OK) goto fail_task;
    s_handler_added = true;
    error = gpio_set_intr_type((gpio_num_t)CONFIG_CARELINK_BUTTON_GPIO,
                               GPIO_INTR_ANYEDGE);
    if (error != ESP_OK) goto fail_handler;
    error = gpio_intr_enable((gpio_num_t)CONFIG_CARELINK_BUTTON_GPIO);
    if (error != ESP_OK) goto fail_handler;
    s_started = true;
    ESP_LOGI(TAG, "SOS button ready on GPIO%d: pressed=%s",
             CONFIG_CARELINK_BUTTON_GPIO,
             s_logic.stable_active ? "yes" : "no");
    return ESP_OK;

fail_handler:
    gpio_isr_handler_remove((gpio_num_t)CONFIG_CARELINK_BUTTON_GPIO);
    s_handler_added = false;
fail_task:
    vTaskDelete(s_task);
    s_task = NULL;
fail_queue:
    s_event_queue = NULL;
fail_gpio:
    gpio_reset_pin((gpio_num_t)CONFIG_CARELINK_BUTTON_GPIO);
    return error;
}

esp_err_t carelink_button_set_callback(carelink_button_event_callback_t callback,
                                       void *context)
{
    taskENTER_CRITICAL(&s_lock);
    s_callback = callback;
    s_callback_context = callback != NULL ? context : NULL;
    taskEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t carelink_button_receive(carelink_button_event_t *event, uint32_t timeout_ms)
{
    if (event == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_started) return ESP_ERR_INVALID_STATE;
    TickType_t ticks;
    if (timeout_ms == UINT32_MAX) {
        ticks = portMAX_DELAY;
    } else if (timeout_ms == 0U) {
        ticks = 0;
    } else {
        ticks = pdMS_TO_TICKS(timeout_ms);
        if (ticks == 0) ticks = 1;
    }
    return xQueueReceive(s_event_queue, event, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t carelink_button_get_stats(carelink_button_stats_t *stats)
{
    if (stats == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_started) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_lock);
    *stats = s_stats;
    taskEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

#else

esp_err_t carelink_button_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_button_set_callback(carelink_button_event_callback_t callback,
                                       void *context)
{
    (void)callback;
    (void)context;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_button_receive(carelink_button_event_t *event, uint32_t timeout_ms)
{
    (void)event;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t carelink_button_get_stats(carelink_button_stats_t *stats)
{
    (void)stats;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
