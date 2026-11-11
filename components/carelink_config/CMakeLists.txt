idf_component_register(
    SRCS "carelink_config.c" "carelink_config_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash)
