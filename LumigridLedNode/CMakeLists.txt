cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "1.0.0-rc2")
endif()

# The registry package contains ignored test files and a .git pointer that Git
# cannot preserve. Restore only the exact known incomplete checkout, with backup.
idf_build_get_property(LUMIGRID_IDF_PYTHON PYTHON)
function(lumigrid_check_managed_components action)
    execute_process(
        COMMAND "${LUMIGRID_IDF_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_managed_components.py"
                --node-root "${CMAKE_CURRENT_SOURCE_DIR}" "${action}"
        RESULT_VARIABLE dependency_check_result)
    if(NOT dependency_check_result EQUAL 0)
        message(FATAL_ERROR "Managed dependency ${action} failed; existing inputs were preserved")
    endif()
endfunction()
lumigrid_check_managed_components(prepare)
project(lumigrid_led_node)
# Pinned Component Manager does not validate a fresh network download before
# writing its marker. Check the actual manifest-filtered bytes before compilation.
lumigrid_check_managed_components(verify)
