idf_component_register(
    SRCS "nodesync.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_event esp_netif mdns effect_control sync_protocol
    PRIV_REQUIRES main
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
