add_library(plasma_lyrics_providers STATIC provider.h)
set_target_properties(plasma_lyrics_providers PROPERTIES LINKER_LANGUAGE CXX POSITION_INDEPENDENT_CODE ON)
target_include_directories(plasma_lyrics_providers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(plasma_lyrics_providers PUBLIC plasma_lyrics_core Qt6::Core)
target_sources(plasma_lyrics_providers PRIVATE
    local/localprovider.cpp
    local/localprovider.h
)

if(ENABLE_PROVIDER_NETEASE)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Network)
    target_sources(plasma_lyrics_providers PRIVATE
        netease/neteaseprovider.cpp
        netease/neteaseprovider.h
    )
    target_link_libraries(plasma_lyrics_providers PUBLIC Qt6::Network)
    target_compile_definitions(plasma_lyrics_providers PUBLIC PLASMA_LYRICS_HAVE_NETEASE=1)
endif()

if(BUILD_TESTING AND ENABLE_PROVIDER_NETEASE)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_executable(tst_neteaseprovider tests/tst_neteaseprovider.cpp)
    target_link_libraries(tst_neteaseprovider PRIVATE plasma_lyrics_providers Qt6::Test)
    add_test(NAME tst_neteaseprovider COMMAND tst_neteaseprovider)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_executable(tst_localprovider tests/tst_localprovider.cpp)
    target_link_libraries(tst_localprovider PRIVATE plasma_lyrics_providers Qt6::Test)
    add_test(NAME tst_localprovider COMMAND tst_localprovider)
endif()
