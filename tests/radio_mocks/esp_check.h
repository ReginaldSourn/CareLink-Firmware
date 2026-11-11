#pragma once

#include "esp_err.h"

#define ESP_RETURN_ON_ERROR(expression, tag, ...) do { \
    (void)(tag); \
    const esp_err_t check_error_ = (expression); \
    if (check_error_ != ESP_OK) return check_error_; \
} while (0)
