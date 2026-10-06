cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "1.0.0-lg0384")
endif()
project(lumigrid_led_node)
