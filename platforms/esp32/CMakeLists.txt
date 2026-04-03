FILE(GLOB app_sources
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader)

set(lib_sources
    ${LIB}/Application.cpp
    ${LIB}/Loop.cpp
    ${LIB}/demos/BouncingBallDemo.cpp
    ${LIB}/demos/MenuDemo.cpp
    ${LIB}/demos/TextShowcaseDemo.cpp
    ${LIB}/demos/PatternDemo.cpp
    ${LIB}/demos/ReaderScreen.cpp
    ${LIB}/demos/BookSelectScreen.cpp
    ${LIB}/content/ZipReader.cpp
    ${LIB}/content/XmlReader.cpp
    ${LIB}/content/CssParser.cpp
    ${LIB}/content/EpubParser.cpp
    ${LIB}/content/TextLayout.cpp
    ${LIB}/content/Book.cpp
    ${LIB}/content/MrbWriter.cpp
    ${LIB}/content/MrbReader.cpp
    ${LIB}/content/MrbConverter.cpp
    ${LIB}/content/ImageDecoder.cpp)

# miniz: use the copy cached by the test build (FetchContent).
# If not available, clone miniz 3.0.2 into test/build2/_deps/miniz-src first.
set(MINIZ_DIR ${CMAKE_CURRENT_LIST_DIR}/../../test/build2/_deps/miniz-src)

idf_component_register(
    SRCS ${app_sources} ${lib_sources} ${MINIZ_DIR}/miniz.c
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../lib" "${MINIZ_DIR}"
    REQUIRES fatfs driver)

# Skip image decoding on ESP32 (saves RAM – no stb_image)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MICROREADER_NO_IMAGES)
