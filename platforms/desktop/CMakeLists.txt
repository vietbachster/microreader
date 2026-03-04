cmake_minimum_required(VERSION 3.16)
project(microreader_desktop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# SDL2 – fetched at configure time; static build, no tests, no shared lib.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.10
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# ---------------------------------------------------------------------------
# Platform-agnostic core library
# ---------------------------------------------------------------------------
add_library(microreader_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/microreader/Application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/microreader/Loop.cpp
)
target_include_directories(microreader_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib
)

# ---------------------------------------------------------------------------
# Desktop executable
# ---------------------------------------------------------------------------
add_executable(microreader_desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)
target_link_libraries(microreader_desktop PRIVATE
    microreader_core
    SDL2::SDL2-static
)
# Prevent SDL_main.h from renaming main() so we keep a plain console entry point.
target_compile_definitions(microreader_desktop PRIVATE SDL_MAIN_HANDLED)
