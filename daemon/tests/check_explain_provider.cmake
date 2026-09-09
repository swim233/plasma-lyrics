execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${CONFIG_HOME}"
            "XDG_CACHE_HOME=${CACHE_HOME}"
            "${DAEMON}" --explain --provider __missing__ Song Artist
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
    message(FATAL_ERROR "an unavailable explicit provider unexpectedly succeeded")
endif()

set(combined "${output}${error}")
if(NOT combined MATCHES "available providers: .*${EXPECTED_PROVIDER}")
    message(FATAL_ERROR
        "provider diagnostics did not list ${EXPECTED_PROVIDER}:\n${combined}")
endif()

# Exercise the real AMLL explanation path without depending on the network.
# This is run by both the default and ENABLE_PROVIDER_NETEASE=OFF matrices, so
# the latter proves that --explain has no hidden NetEase dependency.
if(EXPECTED_PROVIDER STREQUAL "amll")
    file(MAKE_DIRECTORY "${CACHE_HOME}/plasma-lyrics")
    file(WRITE "${CACHE_HOME}/plasma-lyrics/amll-index.jsonl"
        "{\"metadata\":[[\"album\",[\"Album\"]],[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]]],\"rawLyricFile\":\"1000-explain.ttml\"}\n")
    file(WRITE "${CACHE_HOME}/plasma-lyrics/amll-index.jsonl.meta"
        "{\"fetchedAt\":4102444800,\"sourceIndexUrl\":\"https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/metadata/raw-lyrics-index.jsonl\",\"sourceContentBaseUrl\":\"https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main\"}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${CONFIG_HOME}"
                "XDG_CACHE_HOME=${CACHE_HOME}"
                "${DAEMON}" --explain --provider amll Song Artist
        RESULT_VARIABLE amll_result
        OUTPUT_VARIABLE amll_output
        ERROR_VARIABLE amll_error)
    if(NOT amll_result EQUAL 0
       OR NOT amll_output MATCHES "provider chain: amll"
       OR NOT amll_output MATCHES "provider: amll"
       OR NOT amll_output MATCHES "match policy: preserve-versions"
       OR NOT amll_output MATCHES "versionTier=normal"
       OR NOT amll_output MATCHES "selected: netease:42")
        message(FATAL_ERROR
            "AMLL-only explanation did not complete a real match:\n${amll_output}${amll_error}")
    endif()
endif()
