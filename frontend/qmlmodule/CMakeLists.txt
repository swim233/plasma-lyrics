add_library(plasmalyricssource)

ecm_add_qml_module(plasmalyricssource
    URI "io.github.swim233.lyrics"
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/io/github/swim233/lyrics
)

target_sources(plasmalyricssource PRIVATE
    backendconfig.cpp
    backendconfig.h
    lyricsource.cpp
    lyricsource.h
)
target_include_directories(plasmalyricssource PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(plasmalyricssource PRIVATE plasma_lyrics_core Qt6::Core Qt6::Qml Qt6::Sql)

ecm_finalize_qml_module(plasmalyricssource DESTINATION ${KDE_INSTALL_QMLDIR})
install(TARGETS plasmalyricssource ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_executable(tst_lyricsource
        tests/tst_lyricsource.cpp
        lyricsource.cpp
    )
    target_include_directories(tst_lyricsource PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_lyricsource PRIVATE plasma_lyrics_core Qt6::Core Qt6::Qml Qt6::Sql Qt6::Test)
    add_test(NAME tst_lyricsource COMMAND tst_lyricsource)
endif()

