find_package(Qt6 6.6 REQUIRED COMPONENTS DBus Network)

add_executable(plasma-lyricsd
    src/config.cpp
    src/config.h
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
target_link_libraries(plasma-lyricsd PRIVATE
    plasma_lyrics_core
    plasma_lyrics_providers
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    Qt6::Sql
)
install(TARGETS plasma-lyricsd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_executable(tst_mprispolicy
        tests/tst_mprispolicy.cpp
        src/mpris/mprispolicy.cpp
    )
    target_include_directories(tst_mprispolicy PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_mprispolicy PRIVATE plasma_lyrics_core Qt6::Test)
    add_test(NAME tst_mprispolicy COMMAND tst_mprispolicy)

    # The bug this covers lives in the D-Bus wiring itself, so the test needs a
    # real bus. dbus-run-session gives it a private one, which also keeps the
    # developer's own running players out of the assertions.
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    if(DBUS_RUN_SESSION_EXECUTABLE)
        add_executable(tst_mprisdiscovery
            tests/tst_mprisdiscovery.cpp
            src/mpris/mprismanager.cpp
            src/mpris/mprisplayer.cpp
            src/mpris/mprispolicy.cpp
        )
        target_include_directories(tst_mprisdiscovery PRIVATE ${CMAKE_SOURCE_DIR})
        target_link_libraries(tst_mprisdiscovery PRIVATE plasma_lyrics_core Qt6::DBus Qt6::Test)
        add_test(NAME tst_mprisdiscovery
                 COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst_mprisdiscovery>)
    else()
        message(STATUS "dbus-run-session not found -- skipping tst_mprisdiscovery")
    endif()

    add_executable(tst_snapshot
        tests/tst_snapshot.cpp
        src/snapshot.cpp
    )
    target_include_directories(tst_snapshot PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_snapshot PRIVATE plasma_lyrics_core Qt6::Test)
    add_test(NAME tst_snapshot COMMAND tst_snapshot)

    add_executable(tst_resolver
        tests/tst_resolver.cpp
        src/resolver.cpp
    )
    target_include_directories(tst_resolver PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tst_resolver PRIVATE
        plasma_lyrics_core
        plasma_lyrics_providers
        Qt6::Test
    )
    add_test(NAME tst_resolver COMMAND tst_resolver)
endif()
