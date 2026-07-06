cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(
  WorldInBox
  VERSION 1.0.0
  LANGUAGES C)

set(PROGRAM_NAME game)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(raylib REQUIRED QUIET)

set(SOURCES src/main.c)

add_executable(${PROGRAM_NAME} ${SOURCES})

target_link_libraries(${PROGRAM_NAME}  PRIVATE raylib)

target_compile_options(${PROGRAM_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
set_target_properties(${PROGRAM_NAME} PROPERTIES C_EXTENSIONS OFF)
