cmake_minimum_required(VERSION 3.15.0)

# NOTE not directly a component, so it needs to be referenced separately
list(APPEND EXTRA_COMPONENT_DIRS libraries/mpu9250/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-motion-reporter-poc)
