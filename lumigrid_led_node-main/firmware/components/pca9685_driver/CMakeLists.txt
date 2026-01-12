idf_component_register(
    SRCS "pca9685_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
