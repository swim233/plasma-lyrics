# Horizontal extension point 1: one CMake option per provider (DESIGN #23),
# all wired through the Provider interface in provider.h. netease is the only
# online source in the first version; local reads .lrc files and the
# overrides/ directory; lrclib is a placeholder with the interface left in.
set(lyricsproviders_SRC
    provider.h)

if(PROVIDER_NETEASE)
    list(APPEND lyricsproviders_SRC
        netease/NeteaseApi.cpp
        netease/NeteaseApi.h
        netease/NeteaseParse.cpp
        netease/NeteaseParse.h
        netease/NeteaseProvider.cpp
        netease/NeteaseProvider.h)
endif()

if(PROVIDER_LRCLIB)
    list(APPEND lyricsproviders_SRC
        lrclib/LrclibProvider.cpp
        lrclib/LrclibProvider.h)
endif()

add_library(lyricsproviders STATIC ${lyricsproviders_SRC})
target_include_directories(lyricsproviders PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lyricsproviders PUBLIC lyricscore Qt6::Core Qt6::Network)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
