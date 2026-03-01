# IDF component – wraps the platform-agnostic core library.
idf_component_register(
  SRCS
    "${CMAKE_CURRENT_LIST_DIR}/../../core/src/Application.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../core/src/Loop.cpp"
  INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../core/include"
)
