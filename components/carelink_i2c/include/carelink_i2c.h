#pragma once

#include "driver/i2c_master.h"

/* Obtain the OLED/header shared I2C0 bus. Initializes once, enables Vext, and
 * retains the bus for firmware lifetime. Call only during serialized startup
 * from app_main, before consumers. Callers must never delete this bus. */
esp_err_t carelink_i2c_get_shared_bus(i2c_master_bus_handle_t *bus);
