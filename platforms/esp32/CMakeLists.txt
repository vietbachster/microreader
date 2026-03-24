FILE(GLOB app_sources
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(lib_sources
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader/Application.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader/Demo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader/Loop.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader/LutCalibration.cpp)

idf_component_register(SRCS ${app_sources} ${lib_sources}
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../lib")
