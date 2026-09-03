# Provider fixtures: real API responses recorded on this machine (DESIGN §6).
# They carry the dirty shapes the parser must survive offline.
set(providers_fixture_files
    fixtures/netease/search-laojiebei.json
    fixtures/netease/search-mofachuniang.json
    fixtures/netease/lyric-1299289240.json
    fixtures/netease/lyric-186016.json)

# Copy into the build tree so tests read a stable path.
foreach(f ${providers_fixture_files})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${f}
                   ${CMAKE_CURRENT_BINARY_DIR}/${f} COPYONLY)
endforeach()

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Test)

ecm_add_test(
    tst_netease_parse.cpp
    TEST_NAME netease_parse
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricsproviders)
