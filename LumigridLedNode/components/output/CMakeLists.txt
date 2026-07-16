idf_component_register(SRCS "AddressableLeds.cpp" "Pca9685.cpp" "OutputController.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES board espressif__led_strip)
