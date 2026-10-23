#include "carelink_imu_state.h"

carelink_imu_state_t carelink_imu_classify(const carelink_imu_snapshot_t *snapshot,
                                         int64_t now_us, int64_t stale_us)
{
    if (!snapshot || stale_us <= 0) return CARELINK_IMU_ERROR;
    if (snapshot->fault) return CARELINK_IMU_ERROR;
    if (snapshot->sample_time_us < 0) return CARELINK_IMU_NO_DATA;
    if (now_us < snapshot->sample_time_us ||
        now_us - snapshot->sample_time_us >= stale_us) return CARELINK_IMU_STALE;
    return CARELINK_IMU_READY;
}

const char *carelink_imu_state_name(carelink_imu_state_t state)
{
    switch (state) {
    case CARELINK_IMU_NO_DATA: return "no data";
    case CARELINK_IMU_READY: return "ready";
    case CARELINK_IMU_STALE: return "stale";
    case CARELINK_IMU_ERROR: return "error";
    default: return "unknown";
    }
}
