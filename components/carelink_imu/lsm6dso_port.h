#pragma once

#include "driver/i2c_master.h"
#include "lsm6dso_device.h"

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
    bool owns_bus;
    esp_err_t last_error;
} carelink_lsm6dso_port_t;

esp_err_t carelink_lsm6dso_port_open(carelink_lsm6dso_port_t *port,
                                    carelink_lsm6dso_t *device, uint8_t address);
void carelink_lsm6dso_port_close(carelink_lsm6dso_port_t *port);
