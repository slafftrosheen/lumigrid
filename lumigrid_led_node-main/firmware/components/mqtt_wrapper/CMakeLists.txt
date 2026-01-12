idf_component_register(
    SRCS "mqtt_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
