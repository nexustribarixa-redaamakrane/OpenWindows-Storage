cmake_minimum_required(VERSION 3.10)
project(ow_storage C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_subdirectory(common)
add_subdirectory(owfs)
add_subdirectory(usfs)
