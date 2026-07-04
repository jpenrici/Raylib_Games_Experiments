cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(
  Spaceship
  VERSION 1.0.0
  LANGUAGES C)

set(PROGRAM_NAME game)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(AUDIO_MUTED "Silence all audio (useful during development/testing)" OFF)
option(ONLY_SHAPE "Hide images if loaded (useful during development/testing)" OFF)

if(EMSCRIPTEN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_GLFW=3 -s ALLOW_MEMORY_GROWTH=1")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(RAYLIB_VERSION 5.5)

set(RAYLIB_SOURCE_DIR "" CACHE PATH
    "Path to an existing raylib source checkout, reused instead of a fresh git clone")

if(RAYLIB_SOURCE_DIR)
  if(NOT EXISTS "${RAYLIB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "RAYLIB_SOURCE_DIR (${RAYLIB_SOURCE_DIR}) doesn't "
                         "look like a raylib checkout: no CMakeLists.txt found there.")
  endif()
  set(FETCHCONTENT_SOURCE_DIR_RAYLIB "${RAYLIB_SOURCE_DIR}")
  message(STATUS "Using local raylib checkout: ${RAYLIB_SOURCE_DIR} (git clone skipped)")
endif()

if(EMSCRIPTEN)
  set(raylib_FOUND FALSE)
else()
  find_package(raylib ${RAYLIB_VERSION} QUIET)
endif()

if(NOT raylib_FOUND)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG ${RAYLIB_VERSION}
    GIT_SHALLOW TRUE)

  # Skip building raylib's own examples/games — we only need the library.
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

  if(EMSCRIPTEN)
    set(PLATFORM "Web" CACHE STRING "" FORCE)
  endif()

  FetchContent_MakeAvailable(raylib)
endif()

set(SOURCES src/main.c)

add_executable(${PROGRAM_NAME} ${SOURCES})

set_target_properties(${PROGRAM_NAME} PROPERTIES C_EXTENSIONS OFF)

if(AUDIO_MUTED)
  target_compile_definitions(${PROGRAM_NAME} PRIVATE AUDIO_MUTED)
  message(STATUS "AUDIO_MUTED: audio disabled for this build")
endif()

if(ONLY_SHAPE)
  target_compile_definitions(${PROGRAM_NAME} PRIVATE ONLY_SHAPE)
  message(STATUS "ONLY_SHAPE: images disabled for this build")
endif()

if(EMSCRIPTEN)
  # Tells our own src/main.c to take the emscripten_set_main_loop() branch.
  target_compile_definitions(${PROGRAM_NAME} PRIVATE PLATFORM_WEB)

  target_link_libraries(${PROGRAM_NAME} PRIVATE raylib)

  set_target_properties(${PROGRAM_NAME} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")

  target_link_options(
    ${PROGRAM_NAME}
    PRIVATE
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/resources@resources"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s USE_GLFW=3"
    "SHELL:-s STACK_SIZE=1048576")
else()
  target_link_libraries(
    ${PROGRAM_NAME}
    PRIVATE raylib
            wayland-client
            wayland-egl
            EGL
            xkbcommon
            m
            pthread
            dl)

  add_custom_command(
    TARGET ${PROGRAM_NAME}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources
      $<TARGET_FILE_DIR:${PROGRAM_NAME}>/resources
    COMMENT "Copyng resources...")
endif()

target_compile_options(${PROGRAM_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
