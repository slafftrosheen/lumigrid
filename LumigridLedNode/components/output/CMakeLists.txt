idf_component_register(SRCS "ParallelRmtDriver.cpp" "AddressableDriver.cpp" "Pca9685Driver.cpp" "OutputController.cpp" "OutputCoreExecutor.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES board driver esp_timer config factory)
