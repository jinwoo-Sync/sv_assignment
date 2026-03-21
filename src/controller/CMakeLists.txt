cmake_minimum_required(VERSION 3.10)
project(controller)

add_executable(controller src/main.cpp)
target_link_libraries(controller PRIVATE sv_core)
