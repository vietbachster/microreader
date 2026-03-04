FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/platforms/esp32/*.*)

idf_component_register(SRCS ${app_sources})
