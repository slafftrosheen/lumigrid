idf_component_register(SRCS "AddressableLeds.cpp" "Pca9685.cpp" "OutputController.cpp" "OutputArbitrator.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES board espressif__led_strip)
