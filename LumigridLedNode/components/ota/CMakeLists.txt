idf_component_register(
    SRCS "OtaBootVerifier.cpp" "StorageContract.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_update nvs_flash esp_rom freertos config auth spi_flash bootloader_support esp_timer
)
