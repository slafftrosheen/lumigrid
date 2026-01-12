idf_component_register(SRCS "audio_reactive.c"
                       INCLUDE_DIRS "include"
                       REQUIRES log driver esp_adc esp_driver_i2s freertos)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
