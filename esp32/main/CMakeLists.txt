FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/esp32/main/*.*)

idf_component_register(SRCS ${app_sources})
