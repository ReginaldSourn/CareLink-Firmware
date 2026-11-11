#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(milliseconds) \
    ((TickType_t)(((milliseconds) * TEST_TICK_HZ) / 1000U))
