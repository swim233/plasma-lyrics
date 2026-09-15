add_library(plasma_lyrics_providers STATIC provider.h)
set_target_properties(plasma_lyrics_providers PROPERTIES LINKER_LANGUAGE CXX POSITION_INDEPENDENT_CODE ON)
target_include_directories(plasma_lyrics_providers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(plasma_lyrics_providers PUBLIC plasma_lyrics_core Qt6::Core)
target_sources(plasma_lyrics_providers PRIVATE
    local/localprovider.cpp
    local/localprovider.h
    logging.cpp
    logging.h
)

find_package(Qt6 6.6 REQUIRED COMPONENTS Network)
# QRC decoding calls inflate() directly, so link zlib explicitly.
find_package(ZLIB REQUIRED)
target_link_libraries(plasma_lyrics_providers PUBLIC Qt6::Network PRIVATE ZLIB::ZLIB)
target_sources(plasma_lyrics_providers PRIVATE
    netease/neteaseprovider.cpp
    netease/neteaseprovider.h
    amll/amllprovider.cpp
    amll/amllprovider.h
    qq/qqprovider.cpp
    qq/qqprovider.h
    qq/qrccipher.cpp
    qq/qrccipher.h
)

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_executable(tst_neteaseprovider tests/tst_neteaseprovider.cpp)
    target_link_libraries(tst_neteaseprovider PRIVATE plasma_lyrics_providers Qt6::Test)
    target_compile_options(tst_neteaseprovider PRIVATE -Wno-missing-field-initializers)
    # Not QFINDTESTDATA: it resolves against __FILE__, which makepkg
    # rewrites via -ffile-prefix-map when debug packages are enabled, so
    # the fixture goes missing inside an Arch package build and nowhere
    # else. A path CMake hands over is a plain string literal and
    # survives that rewrite.
    target_compile_definitions(tst_neteaseprovider PRIVATE
        PLASMA_LYRICS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME tst_neteaseprovider COMMAND tst_neteaseprovider)
    # See core/tests/CMakeLists.txt: stops Qt's startup locale warning from
    # reaching the real journal via sd_journal_send().
    set_tests_properties(tst_neteaseprovider PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    add_executable(tst_localprovider tests/tst_localprovider.cpp)
    target_link_libraries(tst_localprovider PRIVATE plasma_lyrics_providers Qt6::Test)
    target_compile_options(tst_localprovider PRIVATE -Wno-missing-field-initializers)
    add_test(NAME tst_localprovider COMMAND tst_localprovider)
    set_tests_properties(tst_localprovider PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    add_executable(tst_amllprovider tests/tst_amllprovider.cpp)
    target_link_libraries(tst_amllprovider PRIVATE plasma_lyrics_providers Qt6::Test)
    target_compile_options(tst_amllprovider PRIVATE -Wno-missing-field-initializers)
    add_test(NAME tst_amllprovider COMMAND tst_amllprovider)
    set_tests_properties(tst_amllprovider PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")

    add_executable(tst_qqprovider tests/tst_qqprovider.cpp)
    target_link_libraries(tst_qqprovider PRIVATE plasma_lyrics_providers Qt6::Test)
    target_compile_options(tst_qqprovider PRIVATE -Wno-missing-field-initializers)
    # See the note on tst_neteaseprovider: not QFINDTESTDATA, because
    # -ffile-prefix-map rewrites __FILE__ inside an Arch package build.
    target_compile_definitions(tst_qqprovider PRIVATE
        PLASMA_LYRICS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        PLASMA_LYRICS_QRC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../core/tests/fixtures")
    add_test(NAME tst_qqprovider COMMAND tst_qqprovider)
    set_tests_properties(tst_qqprovider PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
endif()
