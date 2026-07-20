idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        board
        output
        factory
        api
        esp_http_server
        network
        protocols
)

if(COMMAND littlefs_create_partition_image)
    littlefs_create_partition_image(
        littlefs
        "${CMAKE_CURRENT_LIST_DIR}/../web/dist"
        FLASH_IN_PROJECT
    )
endif()
