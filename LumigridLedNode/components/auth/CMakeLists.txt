idf_component_register(
    SRCS "AuthStore.cpp" "AuthManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_rom mbedtls esp_timer freertos
)
