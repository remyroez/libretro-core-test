cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0076 NEW)

# Project
project(test_libretro C CXX)
add_library(${PROJECT_NAME} SHARED)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

find_path(STB_INCLUDE_DIRS "stb.h")
target_include_directories(${PROJECT_NAME} PRIVATE ${STB_INCLUDE_DIRS})

add_subdirectory(code)
