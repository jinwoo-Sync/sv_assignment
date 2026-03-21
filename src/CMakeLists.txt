cmake_minimum_required(VERSION 3.16)
project(sv_assignment CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

find_package(Threads REQUIRED)

add_subdirectory(libs/logger)
add_subdirectory(libs/core)
add_subdirectory(controller)
add_subdirectory(agent)

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(../tests ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
