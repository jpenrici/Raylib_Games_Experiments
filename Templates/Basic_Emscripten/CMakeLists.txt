cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(
  BasicGame
  VERSION 1.0.0
  LANGUAGES C)

# --- Global Configurations ---
set(PROGRAM_NAME game)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build-time options for debugging and development
option(AUDIO_MUTED "Silence all audio (useful during development/testing)" OFF)
option(ONLY_SHAPE "Hide images if loaded (useful during development/testing)" OFF)

# --- Raylib Dependency Management (FetchContent / Local Cache) ---
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(RAYLIB_VERSION 5.5)

# Allows using a pre-downloaded local raylib repository to save bandwidth/time
set(RAYLIB_SOURCE_DIR "" CACHE PATH "Path to an existing raylib source checkout")

if(RAYLIB_SOURCE_DIR)
  if(NOT EXISTS "${RAYLIB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "RAYLIB_SOURCE_DIR invalid: CMakeLists.txt not found at '${RAYLIB_SOURCE_DIR}'.")
  endif()
  set(FETCHCONTENT_SOURCE_DIR_RAYLIB "${RAYLIB_SOURCE_DIR}")
  message(STATUS "Using local raylib checkout: ${RAYLIB_SOURCE_DIR}")
endif()

# Try to find raylib installed on the host system (Desktop only)
if(NOT EMSCRIPTEN)
  find_package(raylib ${RAYLIB_VERSION} QUIET)
endif()

# If not found locally or via find_package, download it from GitHub
if(NOT raylib_FOUND)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG ${RAYLIB_VERSION}
    GIT_SHALLOW TRUE)

  # Disable raylib's internal examples and games to speed up build times
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

  # Force raylib to target the Web platform if compiled with Emscripten
  if(EMSCRIPTEN)
    set(PLATFORM "Web" CACHE STRING "" FORCE)
  endif()

  FetchContent_MakeAvailable(raylib)
endif()

# --- Main Executable Target ---
add_executable(${PROGRAM_NAME} src/main.c)

# Strict compiler flags for code quality
target_compile_options(${PROGRAM_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
set_target_properties(${PROGRAM_NAME} PROPERTIES C_EXTENSIONS OFF)

# Link with raylib (transitive dependencies like WebGL/X11/Wayland are handled by raylib itself)
target_link_libraries(${PROGRAM_NAME} PRIVATE raylib)

# Apply development conditional definitions
if(AUDIO_MUTED)
  target_compile_definitions(${PROGRAM_NAME} PRIVATE AUDIO_MUTED)
  message(STATUS "Development Flag: AUDIO_MUTED is enabled.")
endif()

if(ONLY_SHAPE)
  target_compile_definitions(${PROGRAM_NAME} PRIVATE ONLY_SHAPE)
  message(STATUS "Development Flag: ONLY_SHAPE is enabled.")
endif()

# --- Platform-Specific Post-Processing ---
if(EMSCRIPTEN)
  # Target: Web (WebAssembly / HTML5)
  target_compile_definitions(${PROGRAM_NAME} PRIVATE PLATFORM_WEB)
  
  # Emscripten outputs an index.html file instead of a native binary executable
  set_target_properties(${PROGRAM_NAME} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")

  # Essential linker options for Emscripten virtual filesystem, memory, and shell template
  target_link_options(
    ${PROGRAM_NAME} PRIVATE
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/resources@resources"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s USE_GLFW=3"
    "SHELL:-s STACK_SIZE=1048576")
else()
  # Target: Desktop (Linux, Windows, macOS)
  # Copies the assets directory to the build output folder after a successful compilation
  add_custom_command(
    TARGET ${PROGRAM_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:${PROGRAM_NAME}>/resources
    COMMENT "Copying assets to build directory...")
endif()
