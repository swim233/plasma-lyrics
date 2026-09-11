find_package(Qt6 6.6 REQUIRED COMPONENTS DBus Network)

add_executable(plasma-lyricsd
    src/config.cpp
    src/config.h
    src/controlservice.cpp
    src/controlservice.h
    src/logging.cpp
    src/logging.h
    src/logmirrorformat.cpp
    src/logmirrorformat.h
    src/main.cpp
    src/resolver.cpp
    src/resolver.h
    src/snapshot.cpp
    src/snapshot.h
    src/mpris/mprismanager.cpp
    src/mpris/mprismanager.h
    src/mpris/mprisplayer.cpp
    src/mpris/mprisplayer.h
    src/mpris/mprispolicy.cpp
    src/mpris/mprispolicy.h
    src/mpris/mpristypes.h
)
target_include_directories(plasma-lyricsd PRIVATE ${CMAKE_SOURCE_DIR})
# Wired in rather than retyped in main.cpp, so --version can never drift from
# project(VERSION).
target_compile_definitions(plasma-lyricsd PRIVATE
    PLASMA_LYRICS_VERSION="${PROJECT_VERSION}")
target_link_libraries(plasma-lyricsd PRIVATE
    plasma_lyrics_core
    plasma_lyrics_providers
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    Qt6::Sql
)
install(TARGETS plasma-lyricsd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# kdebugsettings discovers logging categories via KDE_INSTALL_LOGGINGCATEGORIESDIR,
# which only KDEInstallDirs6 (pulled in under BUILD_PLASMOID) provides.
if(BUILD_PLASMOID)
    install(FILES plasma-lyricsd.categories
            DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    # Every test binary below links Qt and so can print Qt's one-time
    # non-UTF8-locale warning at startup; under a systemd-journal-connected
    # environment that warning's default Qt handler reaches the real
    # journal via sd_journal_send() regardless of how ctest captures this
    # process's own stdout/stderr (see core/tests/CMakeLists.txt for the
    # full explanation). LC_ALL=C.UTF-8 removes the warning at the source.
    add_executable(tst_logmirrorformat
        tests/tst_logmirrorformat.cpp
        src/logmirrorformat.cpp
    )
    target_include_directories(tst_logmirrorformat PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_logmirrorformat PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_logmirrorformat COMMAND tst_logmirrorformat)
    set_tests_properties(tst_logmirrorformat PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    add_executable(tst_config
        tests/tst_config.cpp
        src/config.cpp
        src/logging.cpp
    )
    target_include_directories(tst_config PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_config PRIVATE plasma_lyrics_core plasma_lyrics_providers Qt6::Test)
    add_test(NAME tst_config COMMAND tst_config)
    set_tests_properties(tst_config PROPERTIES
        RESOURCE_LOCK plasma_lyrics_config
        ENVIRONMENT "LC_ALL=C.UTF-8")

    add_executable(tst_mprispolicy
        tests/tst_mprispolicy.cpp
        src/mpris/mprispolicy.cpp
    )
    target_include_directories(tst_mprispolicy PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_mprispolicy PRIVATE plasma_lyrics_core Qt6::Test)
    # Not QFINDTESTDATA: it resolves against __FILE__, which makepkg
    # rewrites via -ffile-prefix-map when debug packages are enabled, so
    # the fixture goes missing inside an Arch package build and nowhere
    # else. A path CMake hands over is a plain string literal and
    # survives that rewrite.
    target_compile_definitions(tst_mprispolicy PRIVATE
        PLASMA_LYRICS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME tst_mprispolicy COMMAND tst_mprispolicy)
    set_tests_properties(tst_mprispolicy PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    # The bug this covers lives in the D-Bus wiring itself, so the test needs a
    # real bus. dbus-run-session gives it a private one, which also keeps the
    # developer's own running players out of the assertions.
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    if(DBUS_RUN_SESSION_EXECUTABLE)
        add_executable(tst_mprisdiscovery
            tests/tst_mprisdiscovery.cpp
            src/logging.cpp
            src/mpris/mprismanager.cpp
            src/mpris/mprisplayer.cpp
            src/mpris/mprispolicy.cpp
        )
        target_include_directories(tst_mprisdiscovery PRIVATE ${CMAKE_SOURCE_DIR})
        target_link_libraries(tst_mprisdiscovery PRIVATE plasma_lyrics_core Qt6::DBus Qt6::Test)
        add_test(NAME tst_mprisdiscovery
                 COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst_mprisdiscovery>)
        set_tests_properties(tst_mprisdiscovery PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

        add_executable(tst_mprisplayer_lifecycle
            tests/tst_mprisplayer_lifecycle.cpp
            src/logging.cpp
            src/mpris/mprisplayer.cpp
            src/mpris/mprispolicy.cpp
        )
        target_include_directories(tst_mprisplayer_lifecycle PRIVATE ${CMAKE_SOURCE_DIR})
        target_link_libraries(tst_mprisplayer_lifecycle PRIVATE plasma_lyrics_core Qt6::DBus Qt6::Test)
        add_test(NAME tst_mprisplayer_lifecycle
                 COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst_mprisplayer_lifecycle>)
        set_tests_properties(tst_mprisplayer_lifecycle PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
    else()
        message(STATUS "dbus-run-session not found -- skipping MPRIS D-Bus tests")
    endif()

    add_executable(tst_snapshot
        tests/tst_snapshot.cpp
        src/snapshot.cpp
    )
    target_include_directories(tst_snapshot PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_snapshot PRIVATE plasma_lyrics_core Qt6::Test)
    add_test(NAME tst_snapshot COMMAND tst_snapshot)
    set_tests_properties(tst_snapshot PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    add_executable(tst_resolver
        tests/tst_resolver.cpp
        src/resolver.cpp
        src/logging.cpp
    )
    target_include_directories(tst_resolver PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_resolver PRIVATE
        plasma_lyrics_core
        plasma_lyrics_providers
        Qt6::Test
    )
    add_test(NAME tst_resolver COMMAND tst_resolver)
    set_tests_properties(tst_resolver PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    if(ENABLE_PROVIDER_AMLL)
        set(_explain_expected_provider amll)
    elseif(ENABLE_PROVIDER_NETEASE)
        set(_explain_expected_provider netease)
    else()
        set(_explain_expected_provider none)
    endif()
    add_test(NAME explain-provider-selection
        COMMAND ${CMAKE_COMMAND}
            -DDAEMON=$<TARGET_FILE:plasma-lyricsd>
            -DEXPECTED_PROVIDER=${_explain_expected_provider}
            -DCONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/explain-test-config
            -DCACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/explain-test-cache
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_explain_provider.cmake)
    # This test's own process is `cmake -P`, not the daemon, but
    # execute_process() inside that script inherits ctest's environment
    # for the real plasma-lyricsd invocation -- LC_ALL set here reaches it.
    set_tests_properties(explain-provider-selection PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    if(DBUS_RUN_SESSION_EXECUTABLE)
        add_executable(tst_controlservice
            tests/tst_controlservice.cpp
            src/controlservice.cpp
            src/logging.cpp
            src/resolver.cpp
            src/snapshot.cpp
        )
        target_include_directories(tst_controlservice PRIVATE ${CMAKE_SOURCE_DIR})
        target_link_libraries(tst_controlservice PRIVATE
            plasma_lyrics_core plasma_lyrics_providers Qt6::DBus Qt6::Test)
        add_test(NAME tst_controlservice
                 COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst_controlservice>)
        set_tests_properties(tst_controlservice PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
    endif()
endif()
