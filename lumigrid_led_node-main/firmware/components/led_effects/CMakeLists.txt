idf_component_register(
    SRCS
        "effects.c"
        "effects_pack_basic.c"
        "gamma.c"
        "palettes.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES main
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
