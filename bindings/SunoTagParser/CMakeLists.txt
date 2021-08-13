include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/toolkit
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/asf
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/mpeg
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/ogg
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/ogg/vorbis
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/ogg/flac
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/flac
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/mpc
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/mp4
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/mpeg/id3v2
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/mpeg/id3v2/frames
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/wavpack
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/ogg/speex
        ${CMAKE_CURRENT_SOURCE_DIR}/../../taglib/trueaudio
)

set(sunotagparser_HDRS)

add_library(sunotagparser SHARED SunoTagParser.cpp ${sunotagparser_HDRS})

find_package(Threads REQUIRED)
target_link_libraries(sunotagparser tag Threads::Threads)
set_target_properties(sunotagparser PROPERTIES
        PUBLIC_HEADER "${sunotagparser_HDRS}"
        DEFINE_SYMBOL MAKE_TAGLIB_LIB
        )
