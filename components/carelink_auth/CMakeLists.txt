idf_component_register(
    SRCS "carelink_auth.c" "carelink_auth_mbedtls.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES carelink_protocol mbedtls)
