cmake_minimum_required(VERSION 3.14)
project(sv_assignment VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS  "Build tests"             ON)
option(ENABLE_ASAN  "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)

add_subdirectory(src/libs/core)
add_subdirectory(src/libs/logger)
add_subdirectory(src/controller)
add_subdirectory(src/agent)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
