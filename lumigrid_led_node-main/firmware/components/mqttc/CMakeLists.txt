idf_component_register(
    SRCS "mqttc.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_common json effect_control palettes storage_fs
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
