cmake_minimum_required(VERSION 3.16)

set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "Force response files for large link commands" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS components .managed-components)

project(lumigrid_lednode)

# Create LittleFS partition image from assets/webapp directory
# This will be flashed automatically when running 'idf.py flash'
littlefs_create_partition_image(storage assets/webapp FLASH_IN_PROJECT)
