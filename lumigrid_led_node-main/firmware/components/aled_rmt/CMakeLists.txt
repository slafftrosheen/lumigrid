idf_component_register(
    SRCS "aled_rmt.c"
    INCLUDE_DIRS "include"
    REQUIRES driver led_effects freertos espressif__led_strip
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
