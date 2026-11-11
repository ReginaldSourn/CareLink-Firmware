idf_component_register(
    SRCS "carelink_incident.c"
    INCLUDE_DIRS "include"
    REQUIRES carelink_auth carelink_button carelink_incident_policy
             carelink_protocol carelink_radio carelink_sos carelink_sos_store
             esp_timer esp_system freertos)
