idf_component_register(
    SRCS "ota.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_wifi esp_system esp_timer esp_common app_update
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
