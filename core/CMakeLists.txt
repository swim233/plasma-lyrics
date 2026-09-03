# core: pure logic. QtCore only, QtNetwork/QtDBus banned (DESIGN §6) -- the
# ban is the boundary that makes offline unit tests meaningful.
add_library(lyricscore STATIC
    lyric/LrcParser.cpp
    lyric/LrcParser.h
    lyric/LyricLine.h
    lyric/LyricTimeline.cpp
    lyric/LyricTimeline.h
    store/SearchCache.cpp
    store/SearchCache.h
    lyric/CreditsHeuristic.cpp
    lyric/CreditsHeuristic.h
    match/Scorer.cpp
    match/Scorer.h
    match/KeywordCleaner.cpp
    match/KeywordCleaner.h)
target_include_directories(lyricscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lyricscore PUBLIC Qt6::Core Qt6::Sql PkgConfig::SQLite3)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
