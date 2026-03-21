cmake_minimum_required(VERSION 3.10)
project(agent)

add_executable(agent src/main.cpp)
target_link_libraries(agent PRIVATE sv_core sv_logger)
