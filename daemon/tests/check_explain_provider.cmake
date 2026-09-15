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
if(NOT combined MATCHES "available providers: local -> netease -> amll -> qq\n")
    message(FATAL_ERROR
        "provider diagnostics did not list all built-in providers:\n${combined}")
endif()

# Runtime selection must still support local-only operation with every source built.
set(local_config_home "${CONFIG_HOME}/local-only")
file(MAKE_DIRECTORY "${local_config_home}/plasma-lyrics")
file(WRITE "${local_config_home}/plasma-lyrics/plasma-lyricsd.ini"
    "[providers]\nenabled=local\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${local_config_home}"
            "XDG_CACHE_HOME=${CACHE_HOME}"
            "${DAEMON}" --explain --provider amll Song Artist
    RESULT_VARIABLE local_result
    OUTPUT_VARIABLE local_output
    ERROR_VARIABLE local_error)
if(NOT local_result EQUAL 1
   OR NOT local_error MATCHES "requested provider is unavailable: amll\n"
   OR NOT local_error MATCHES "available providers: local\n")
    message(FATAL_ERROR
        "runtime-disabled providers remained available:\n${local_output}${local_error}")
endif()

# Exercise the real AMLL explanation path without depending on the network.
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
