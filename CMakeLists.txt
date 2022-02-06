cmake_minimum_required(VERSION 3.15.0)

list(APPEND EXTRA_COMPONENT_DIRS libs/esp-mpu9250/components)

add_compile_definitions(U8G2_WITHOUT_FONT_ROTATION)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-motion-reporter-poc)
