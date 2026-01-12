idf_component_register(
    SRCS "effect_control.c"
    INCLUDE_DIRS "include"
    REQUIRES led_effects
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
