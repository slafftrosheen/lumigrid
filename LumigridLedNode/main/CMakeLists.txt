set(MAIN_SRCS
    "main.cpp"
    "LumigridRuntime.cpp"
)

if(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS)
    list(APPEND MAIN_SRCS "RuntimeHardeningTests.cpp")
endif()

idf_component_register(
    SRCS
        ${MAIN_SRCS}
    INCLUDE_DIRS
        "."
    REQUIRES
        board
        output
        config
        factory
        signage
        scheduler
        sync
        api
        esp_http_server
        network
        protocols
        mqtt
        app_update
)

# LittleFS image: enable when web/dist frontend build exists.
# if(COMMAND littlefs_create_partition_image)
#     littlefs_create_partition_image(
#         littlefs
#         "${CMAKE_CURRENT_LIST_DIR}/../web/dist"
#         FLASH_IN_PROJECT
#     )
# endif()
