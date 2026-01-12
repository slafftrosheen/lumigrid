idf_component_register(SRCS "storage_fs.c"
                      INCLUDE_DIRS "include"
                      REQUIRES littlefs)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
