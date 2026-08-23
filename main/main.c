#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "carelink.h"

void app_main(void)
{
    printf("%s firmware starting\n", CARELINK_DEVICE_NAME);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
