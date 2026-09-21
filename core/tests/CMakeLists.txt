function(add_plasma_lyrics_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE plasma_lyrics_core Qt6::Test)
    # Test fixtures build large structs (e.g. tst_matcher.cpp's TrackQuery/
    # Candidate tables) with brace-init that only sets the fields a given
    # case cares about. Every member already carries a default, so this is
    # well-defined aggregate init, not a bug -- unlike the production
    # sites under core/, providers/, daemon/ and tools/, which use C++20
    # designated initializers instead so -Wmissing-field-initializers stays
    # meaningful there. Suppressing it here, per test target rather than
    # globally, avoids rewriting hundreds of test call sites for a warning
    # with nothing to catch.
    target_compile_options(${name} PRIVATE -Wno-missing-field-initializers)
    add_test(NAME ${name} COMMAND ${name})
    # Qt prints one locale warning at startup whenever the ambient locale
    # isn't already UTF-8 (developer shells and CI runners often
    # default to the POSIX "C" locale). Under a systemd-journal-connected
    # environment, that warning's default Qt handler routes straight to the
    # real journal via sd_journal_send() -- independent of process stdio,
    # so ctest capturing this test's own stdout/stderr does not stop it --
    # leaving residue in a real developer journal every time the suite
    # runs. Forcing a UTF-8 locale removes the warning at the source
    # instead of just rerouting it (unlike QT_FORCE_STDERR_LOGGING, which
    # only changes where an unavoidable warning goes).
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
endfunction()

add_plasma_lyrics_test(tst_lrcparser)
add_plasma_lyrics_test(tst_ttmlparser)
target_compile_definitions(tst_ttmlparser PRIVATE
    PLASMA_LYRICS_TTML_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_plasma_lyrics_test(tst_qrcparser)
target_compile_definitions(tst_qrcparser PRIVATE
    PLASMA_LYRICS_QRC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_plasma_lyrics_test(tst_timeline)
add_plasma_lyrics_test(tst_wordsynthesis)
add_plasma_lyrics_test(tst_matcher)
add_plasma_lyrics_test(tst_lyricstore)
add_plasma_lyrics_test(tst_lyricoverridestore)
add_plasma_lyrics_test(tst_proxyspec)
add_plasma_lyrics_test(tst_stringlistsetting)
add_plasma_lyrics_test(tst_configlog)
