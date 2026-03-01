cmake_minimum_required(VERSION 3.16)
project(microreader_desktop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-agnostic core library
add_library(microreader_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/src/Application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/src/Loop.cpp
)
target_include_directories(microreader_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

# Desktop executable
add_executable(microreader_desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)
target_link_libraries(microreader_desktop PRIVATE microreader_core)
