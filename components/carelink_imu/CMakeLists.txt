set(imu_sources "carelink_imu.c" "carelink_imu_state.c")
if(CONFIG_CARELINK_IMU_ENABLED)
    list(APPEND imu_sources "lsm6dso_device.c" "lsm6dso_port.c" "vendor/lsm6dso_reg.c")
endif()
idf_component_register(SRCS ${imu_sources}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "vendor"
                       REQUIRES esp_common
                       PRIV_REQUIRES carelink_board carelink_i2c esp_driver_i2c esp_timer freertos)
