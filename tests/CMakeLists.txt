cmake_minimum_required(VERSION 3.16)
project(carelink_host_tests C)
enable_testing()
option(CARELINK_SANITIZE "Enable address/undefined behavior sanitizers" OFF)
add_executable(test_gps test_gps.c
    ../components/carelink_nmea/carelink_nmea.c
    ../components/carelink_gps/carelink_gps_state.c)
target_include_directories(test_gps PRIVATE
    ../components/carelink_nmea/include ../components/carelink_gps/include)
target_compile_features(test_gps PRIVATE c_std_11)
target_compile_options(test_gps PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
target_link_libraries(test_gps PRIVATE m)
if(CARELINK_SANITIZE)
    target_compile_options(test_gps PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_gps PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME gps_parser_and_freshness COMMAND test_gps)

add_executable(test_oled test_oled.c ../components/carelink_oled/oled_text.c)
target_include_directories(test_oled PRIVATE ../components/carelink_oled)
target_compile_features(test_oled PRIVATE c_std_11)
target_compile_options(test_oled PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
if(CARELINK_SANITIZE)
    target_compile_options(test_oled PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_oled PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME oled_rendering COMMAND test_oled)

add_executable(test_imu_state test_imu_state.c ../components/carelink_imu/carelink_imu_state.c)
add_executable(test_lsm6dso_device test_lsm6dso_device.c
    ../components/carelink_imu/lsm6dso_device.c
    ../components/carelink_imu/vendor/lsm6dso_reg.c)
add_executable(test_imu_port test_imu_port.c
    ../components/carelink_imu/lsm6dso_port.c
    ../components/carelink_i2c/carelink_i2c.c)
target_include_directories(test_imu_port PRIVATE imu_mocks
    ../components/carelink_board/include ../components/carelink_i2c/include)
foreach(target test_imu_state test_lsm6dso_device test_imu_port)
    target_include_directories(${target} PRIVATE ../components/carelink_imu/include
        ../components/carelink_imu ../components/carelink_imu/vendor)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
    target_link_libraries(${target} PRIVATE m)
    if(CARELINK_SANITIZE)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()
add_test(NAME imu_freshness COMMAND test_imu_state)
add_test(NAME lsm6dso_device COMMAND test_lsm6dso_device)
add_test(NAME imu_port_lifecycle COMMAND test_imu_port)
