idf_component_register(
    SRCS "carelink_sos_store.c" "carelink_sos_store_record.c"
    INCLUDE_DIRS "include"
    REQUIRES carelink_sos nvs_flash)
