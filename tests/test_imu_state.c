#include <assert.h>
#include <string.h>
#include "carelink_imu_state.h"

int main(void)
{
    carelink_imu_snapshot_t snapshot = {.sample_time_us = -1};
    assert(carelink_imu_classify(&snapshot, 1000, 500) == CARELINK_IMU_NO_DATA);
    snapshot.sample_time_us = 1000;
    assert(carelink_imu_classify(&snapshot, 1000, 500) == CARELINK_IMU_READY);
    assert(carelink_imu_classify(&snapshot, 1499, 500) == CARELINK_IMU_READY);
    assert(carelink_imu_classify(&snapshot, 1500, 500) == CARELINK_IMU_STALE);
    assert(carelink_imu_classify(&snapshot, 999, 500) == CARELINK_IMU_STALE);
    snapshot.fault = true;
    assert(carelink_imu_classify(&snapshot, 1001, 500) == CARELINK_IMU_ERROR);
    snapshot.sample_time_us = -1;
    assert(carelink_imu_classify(&snapshot, 1001, 500) == CARELINK_IMU_ERROR);
    snapshot.fault = false;
    snapshot.sample_time_us = 1600;
    snapshot.sequence = 0; /* Sequence wrap must not turn a real sample into NO_DATA. */
    assert(carelink_imu_classify(&snapshot, 1601, 500) == CARELINK_IMU_READY);
    assert(carelink_imu_classify(NULL, 0, 500) == CARELINK_IMU_ERROR);
    assert(carelink_imu_classify(&snapshot, 1601, 0) == CARELINK_IMU_ERROR);
    assert(strcmp(carelink_imu_state_name(CARELINK_IMU_READY), "ready") == 0);
    return 0;
}
