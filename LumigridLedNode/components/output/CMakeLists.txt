idf_component_register(SRCS "AddressableDriver.cpp" "Pca9685Driver.cpp" "OutputController.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES board espressif__led_strip esp_timer config factory)
