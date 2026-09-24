idf_component_register(
    SRCS "CueRunner.cpp" "ShowTransport.cpp"
    INCLUDE_DIRS "include"
    REQUIRES signage config control esp_timer esp_hw_support
)
