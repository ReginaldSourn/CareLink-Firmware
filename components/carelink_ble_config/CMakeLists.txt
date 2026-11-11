set(carelink_ble_private_requires esp_common)
if(CONFIG_CARELINK_BLE_CONFIG_ENABLED)
    list(APPEND carelink_ble_private_requires
         bt esp_hw_support esp_system freertos)
endif()

idf_component_register(
    SRCS "carelink_ble_config.c"
    INCLUDE_DIRS "include"
    REQUIRES carelink_config carelink_incident
    PRIV_REQUIRES ${carelink_ble_private_requires})

if(CONFIG_CARELINK_BLE_CONFIG_ENABLED)
    idf_component_optional_requires(PRIVATE esp_local_ctrl protocomm)
endif()
