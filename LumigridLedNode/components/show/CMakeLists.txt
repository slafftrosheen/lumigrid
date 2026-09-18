idf_component_register(
    SRCS "CueRunner.cpp"
    INCLUDE_DIRS "include"
    REQUIRES signage config control esp_timer esp_hw_support
)
