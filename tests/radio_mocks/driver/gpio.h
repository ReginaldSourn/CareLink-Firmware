#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef int gpio_num_t;
typedef struct {
    uint64_t pin_bit_mask;
    int mode;
} gpio_config_t;

#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) <= 48)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) GPIO_IS_VALID_GPIO(pin)

esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
