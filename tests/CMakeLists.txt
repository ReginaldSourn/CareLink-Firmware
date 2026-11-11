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

add_executable(test_button test_button.c
    ../components/carelink_button/carelink_button_logic.c)
target_include_directories(test_button PRIVATE ../components/carelink_button/include)
target_compile_features(test_button PRIVATE c_std_11)
target_compile_options(test_button PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
if(CARELINK_SANITIZE)
    target_compile_options(test_button PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_button PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME sos_button_logic COMMAND test_button)

add_executable(test_protocol test_protocol.c
    ../components/carelink_protocol/carelink_protocol.c)
target_include_directories(test_protocol PRIVATE
    ../components/carelink_protocol/include)

add_executable(test_sos test_sos.c ../components/carelink_sos/carelink_sos.c)
target_include_directories(test_sos PRIVATE ../components/carelink_sos/include)

foreach(target test_protocol test_sos)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
    if(CARELINK_SANITIZE)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()
add_test(NAME carelink_protocol_codec COMMAND test_protocol)
add_test(NAME carelink_sos_fsm COMMAND test_sos)

add_executable(test_sos_store_record test_sos_store_record.c
    ../components/carelink_sos_store/carelink_sos_store_record.c)
target_include_directories(test_sos_store_record PRIVATE
    ../components/carelink_sos_store/include
    ../components/carelink_sos/include)
target_compile_features(test_sos_store_record PRIVATE c_std_11)
target_compile_options(test_sos_store_record PRIVATE
    -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
if(CARELINK_SANITIZE)
    target_compile_options(test_sos_store_record PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_sos_store_record PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME carelink_sos_store_record COMMAND test_sos_store_record)

add_executable(test_incident_policy test_incident_policy.c
    ../components/carelink_incident_policy/carelink_incident_policy.c
    ../components/carelink_protocol/carelink_protocol.c)
target_include_directories(test_incident_policy PRIVATE
    ../components/carelink_incident_policy/include
    ../components/carelink_protocol/include
    ../components/carelink_sos/include)
target_compile_features(test_incident_policy PRIVATE c_std_11)
target_compile_options(test_incident_policy PRIVATE
    -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
if(CARELINK_SANITIZE)
    target_compile_options(test_incident_policy PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_incident_policy PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME carelink_incident_policy COMMAND test_incident_policy)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
add_executable(test_auth test_auth.c
    ../components/carelink_auth/carelink_auth.c
    auth_mocks/carelink_auth_crypto_openssl.c)
target_include_directories(test_auth PRIVATE
    ../components/carelink_auth/include
    ../components/carelink_auth
    ../components/carelink_protocol/include)
target_compile_features(test_auth PRIVATE c_std_11)
target_compile_options(test_auth PRIVATE
    -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
target_link_libraries(test_auth PRIVATE OpenSSL::Crypto)
if(CARELINK_SANITIZE)
    target_compile_options(test_auth PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_auth PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME carelink_auth_hmac COMMAND test_auth)

add_executable(test_security test_security.c
    ../components/carelink_security/carelink_security.c)
target_include_directories(test_security PRIVATE
    security_mocks
    ../components/carelink_security/include
    ../components/carelink_auth/include)
target_compile_features(test_security PRIVATE c_std_11)
target_compile_options(test_security PRIVATE
    -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
if(CARELINK_SANITIZE)
    target_compile_options(test_security PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_security PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME carelink_security_storage COMMAND test_security)

foreach(radio_profile v42_r2 v431_r2 r8)
    foreach(tick_hz 100 1000)
        set(radio_target test_radio_${radio_profile}_${tick_hz}hz)
        add_executable(${radio_target} test_radio.c)
        target_include_directories(${radio_target} PRIVATE radio_mocks
            ../components/carelink_radio/include
            ../components/carelink_radio)
        target_compile_definitions(${radio_target} PRIVATE TEST_TICK_HZ=${tick_hz})
        if(radio_profile STREQUAL "v42_r2")
            target_compile_definitions(${radio_target} PRIVATE TEST_RADIO_PROFILE_V42_R2=1)
        elseif(radio_profile STREQUAL "r8")
            target_compile_definitions(${radio_target} PRIVATE TEST_RADIO_PROFILE_R8=1)
        endif()
        target_compile_features(${radio_target} PRIVATE c_std_11)
        target_compile_options(${radio_target} PRIVATE
            -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
        if(CARELINK_SANITIZE)
            target_compile_options(${radio_target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${radio_target} PRIVATE -fsanitize=address,undefined)
        endif()
        add_test(NAME carelink_radio_${radio_profile}_${tick_hz}hz
                 COMMAND ${radio_target})
    endforeach()
endforeach()
