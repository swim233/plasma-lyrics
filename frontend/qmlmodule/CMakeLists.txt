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
    globalconfig.cpp
    globalconfig.h
    lyricsource.cpp
    lyricsource.h
    visibilitypolicy.cpp
    visibilitypolicy.h
)
target_include_directories(plasmalyricssource PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(plasmalyricssource PRIVATE plasma_lyrics_core Qt6::Core Qt6::DBus Qt6::Qml Qt6::Sql KF6::I18n)

ecm_finalize_qml_module(plasmalyricssource DESTINATION ${KDE_INSTALL_QMLDIR})
install(TARGETS plasmalyricssource ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    add_executable(tst_backendconfig
        tests/tst_backendconfig.cpp
        backendconfig.cpp
    )
    target_include_directories(tst_backendconfig PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_backendconfig PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME tst_backendconfig COMMAND tst_backendconfig)

    add_executable(tst_globalconfig
        tests/tst_globalconfig.cpp
        globalconfig.cpp
    )
    target_include_directories(tst_globalconfig PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_globalconfig PRIVATE plasma_lyrics_core Qt6::DBus Qt6::Qml Qt6::Test)
    if(DBUS_RUN_SESSION_EXECUTABLE)
        add_test(NAME tst_globalconfig
                 COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst_globalconfig>)
    else()
        add_test(NAME tst_globalconfig COMMAND tst_globalconfig)
    endif()

    add_executable(tst_lyricsource
        tests/tst_lyricsource.cpp
        lyricsource.cpp
    )
    target_include_directories(tst_lyricsource PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_lyricsource PRIVATE plasma_lyrics_core Qt6::Core Qt6::DBus Qt6::Qml Qt6::Sql Qt6::Test KF6::I18n)
    if(DBUS_RUN_SESSION_EXECUTABLE)
        add_test(NAME tst_lyricsource
                 COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst_lyricsource>)
    else()
        add_test(NAME tst_lyricsource COMMAND tst_lyricsource)
    endif()

    add_executable(tst_visibilitypolicy
        tests/tst_visibilitypolicy.cpp
        visibilitypolicy.cpp
    )
    target_include_directories(tst_visibilitypolicy PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_visibilitypolicy PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME tst_visibilitypolicy COMMAND tst_visibilitypolicy)
endif()
