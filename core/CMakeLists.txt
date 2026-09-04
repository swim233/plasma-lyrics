add_library(plasma_lyrics_core STATIC
    lyric/lyricmodel.cpp
    lyric/lyricmodel.h
    lyric/lrcparser.cpp
    lyric/lrcparser.h
    lyric/timeline.cpp
    lyric/timeline.h
    match/matcher.cpp
    match/matcher.h
    store/lyricstore.cpp
    store/lyricstore.h
)
set_target_properties(plasma_lyrics_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(plasma_lyrics_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(plasma_lyrics_core PUBLIC Qt6::Core Qt6::Sql)
target_compile_definitions(plasma_lyrics_core PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

