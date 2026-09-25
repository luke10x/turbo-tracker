cmake_minimum_required(VERSION 3.14)

project(turbo-tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TV_BUILD_EXAMPLES OFF CACHE BOOL "Build Turbo Vision examples" FORCE)
set(TV_BUILD_TESTS OFF CACHE BOOL "Build Turbo Vision tests" FORCE)
set(TV_BUILD_AVSCOLOR OFF CACHE BOOL "Build Turbo Vision avscolor example" FORCE)

add_subdirectory(../tvision "${CMAKE_BINARY_DIR}/tvision")

add_executable(turbo-tracker src/tracker.cpp)
target_link_libraries(turbo-tracker PRIVATE tvision::tvision)
