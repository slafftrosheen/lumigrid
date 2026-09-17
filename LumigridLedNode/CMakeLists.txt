cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.0.0")
project(lumigrid_led_node)
add_compile_definitions(CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS=1)

