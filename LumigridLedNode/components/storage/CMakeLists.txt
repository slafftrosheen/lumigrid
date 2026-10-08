idf_component_register(
    SRCS "NvsRuntime.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_common log
)
