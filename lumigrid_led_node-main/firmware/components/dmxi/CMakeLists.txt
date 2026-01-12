idf_component_register(
    SRCS
        "dmxi.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        json
        storage_fs
        layout
        esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Werror=format
    -Werror=int-conversion
)
