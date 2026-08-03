idf_component_register(SRCS "ClockDiscipline.cpp" "NodeSync.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES signage config esp_timer lwip)
