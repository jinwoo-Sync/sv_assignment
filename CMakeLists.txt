cmake_minimum_required(VERSION 3.14)
project(sv_assignment VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build" FORCE)
endif()

set(BASE_DIRECTORY ${CMAKE_SOURCE_DIR})

if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BASE_DIRECTORY}/lib/${CMAKE_BUILD_TYPE})
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BASE_DIRECTORY}/bin/${CMAKE_BUILD_TYPE})
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BASE_DIRECTORY}/bin/${CMAKE_BUILD_TYPE})
endif()

option(BUILD_TESTS  "Build tests"              ON)
option(ENABLE_ASAN  "Enable AddressSanitizer"  OFF)
option(ENABLE_UBSAN "Enable UB Sanitizer"      OFF)

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)

set(SV_ALL ON)
add_subdirectory(src/libs)
add_subdirectory(src/controller)
add_subdirectory(src/agent)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
