idf_component_register(
    SRCS "palettes.c"
    INCLUDE_DIRS "include"
    REQUIRES json esp_common storage_fs
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
