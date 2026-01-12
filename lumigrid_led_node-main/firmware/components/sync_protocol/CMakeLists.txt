idf_component_register(
    SRCS "sync_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES lwip esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
