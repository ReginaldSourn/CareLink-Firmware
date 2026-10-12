#pragma once

#include "carelink_gps_state.h"

void carelink_display_start(void);
void carelink_display_gps(const carelink_gps_snapshot_t *gps);
void carelink_display_gps_error(void);
