function(add_plasma_lyrics_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE plasma_lyrics_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_plasma_lyrics_test(tst_lrcparser)
add_plasma_lyrics_test(tst_timeline)
add_plasma_lyrics_test(tst_matcher)
add_plasma_lyrics_test(tst_lyricstore)

