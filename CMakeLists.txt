cmake_minimum_required(VERSION 3.10)

# set(SDK_PATH "$ENV{IOT_CORE_PATH}" CACHE PATH "Project root directory" FORCE)

get_filename_component(SDK_PATH
        "${CMAKE_CURRENT_LIST_DIR}/EmbeddedCore/IOTCoreBase/" 
        REALPATH
    )
# set(PROJECT_VER "2.0.0.0") 

# Now include ESP-IDF project helpers
list(APPEND SDKCONFIG_DEFAULTS
                "${CMAKE_SOURCE_DIR}/sdkconfig.defaults"
                "${CMAKE_SOURCE_DIR}/sdkconfig.defaults.esp32c3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project
project(esp32App)