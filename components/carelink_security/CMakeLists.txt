idf_component_register(
    SRCS "carelink_security.c"
    INCLUDE_DIRS "include"
    REQUIRES carelink_auth esp_system nvs_flash)
