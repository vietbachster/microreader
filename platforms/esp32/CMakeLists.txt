FILE(GLOB app_sources
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib/microreader)

set(lib_sources
    ${LIB}/Application.cpp
    ${LIB}/Loop.cpp
    ${LIB}/screens/BouncingBallDemo.cpp
    ${LIB}/screens/GrayscaleDemo.cpp
    ${LIB}/screens/ListMenuScreen.cpp
    ${LIB}/screens/MainMenu.cpp
    ${LIB}/screens/ReaderScreen.cpp
    ${LIB}/screens/SettingsScreen.cpp
    ${LIB}/screens/ChapterSelectScreen.cpp
    ${LIB}/screens/ReaderOptionsScreen.cpp
    ${LIB}/content/ZipReader.cpp
    ${LIB}/content/XmlReader.cpp
    ${LIB}/content/CssParser.cpp
    ${LIB}/content/EpubParser.cpp
    ${LIB}/content/TextLayout.cpp
    ${LIB}/content/hyphenation/Hyphenation.cpp
    ${LIB}/content/Book.cpp
    ${LIB}/content/mrb/MrbWriter.cpp
    ${LIB}/content/mrb/MrbReader.cpp
    ${LIB}/content/mrb/MrbConverter.cpp
    ${LIB}/content/ImageDecoder.cpp
    ${LIB}/content/JpegDecoder.cpp
    ${LIB}/content/PngDecoder.cpp)

# miniz: use the copy cached by the test build (FetchContent).
# If not available, clone miniz 3.0.2 into test/build2/_deps/miniz-src first.
set(MINIZ_DIR ${CMAKE_CURRENT_LIST_DIR}/../../test/build2/_deps/miniz-src)

idf_component_register(
    SRCS ${app_sources} ${lib_sources} ${MINIZ_DIR}/miniz.c
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../lib" "${MINIZ_DIR}"
    REQUIRES fatfs driver wear_levelling)


