#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "carelink_gps_state.h"
#include "carelink_incident.h"

typedef struct {
    carelink_gps_snapshot_t gps;
    carelink_incident_status_t incident;
    uint32_t battery_mv;
    bool gps_available;
    bool incident_available;
    bool battery_available;
} carelink_display_status_t;

void carelink_display_start(void);
void carelink_display_update(const carelink_display_status_t *status);
