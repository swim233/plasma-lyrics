add_executable(import-waylyrics main.cpp)
target_include_directories(import-waylyrics PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(import-waylyrics PRIVATE plasma_lyrics_core Qt6::Core Qt6::Sql)
# In bindir, not libexecdir: the README tells the user to run this by hand, and
# libexecdir is multiarch-qualified on Debian, so a documented absolute path
# under it is wrong on half the distributions.
set_target_properties(import-waylyrics PROPERTIES
    OUTPUT_NAME plasma-lyrics-import-waylyrics)
install(TARGETS import-waylyrics RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_test(NAME import-waylyrics-fixture
             COMMAND import-waylyrics --dry-run
                     --source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture.json)
    set_tests_properties(import-waylyrics-fixture PROPERTIES
        PASS_REGULAR_EXPRESSION "imported=1 skipped=0 failed=0")
endif()
