find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Test)

ecm_add_test(
    tst_lrcparser.cpp
    TEST_NAME lrcparser
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricscore)

ecm_add_test(
    tst_timeline.cpp
    TEST_NAME timeline
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricscore)

ecm_add_test(
    tst_credits.cpp
    TEST_NAME credits
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricscore)

ecm_add_test(
    tst_keywords.cpp
    TEST_NAME keywords
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricscore)

ecm_add_test(
    tst_scorer.cpp
    TEST_NAME scorer
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricscore)

ecm_add_test(
    tst_searchcache.cpp
    TEST_NAME searchcache
    LINK_LIBRARIES Qt6::Core Qt6::Test lyricscore)

