# A.3e item 1: --explain is a read-only diagnostic and must never touch the
# settings file, and in particular must not run Config::migrateLegacySettings()
# (which calls QSettings::sync() unconditionally). Isolated via
# XDG_CONFIG_HOME + `cmake -E env`, the same mechanism check_explain_provider.cmake
# already uses -- never HOME, and never the real user config.

set(_ini "${CONFIG_HOME}/plasma-lyrics/plasma-lyricsd.ini")
file(MAKE_DIRECTORY "${CONFIG_HOME}/plasma-lyrics")
# A leftover `@Invalid()` literal on all three Q24/A.3a-A.3b keys: if
# migrateLegacySettings() ran on this path, every one of these lines would be
# rewritten (players/blacklist and filter/musicUrlPrefixes removed entirely,
# filter/platforms replaced with the marker).
file(WRITE "${_ini}"
    "[players]\nblacklist=@Invalid()\n\n[filter]\nmusicUrlPrefixes=@Invalid()\nplatforms=@Invalid()\n")

file(READ "${_ini}" _before_contents)

execute_process(
    # --provider local: keeps this test fast, deterministic and free of any
    # real network dependency (netease/amll would otherwise both be queried
    # too) -- this test only needs to prove Config was constructed and the
    # real --explain path ran, not exercise multi-provider matching.
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${CONFIG_HOME}"
            "XDG_CACHE_HOME=${CACHE_HOME}"
            "${DAEMON}" --explain --provider local Song Artist
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

file(READ "${_ini}" _after_contents)

# Sanity check that --explain actually ran the real path (constructed
# Config, built the provider chain) rather than the file staying untouched
# because the process failed before getting there. Exit code is not part of
# this check: 1 is the normal, expected result for a query that matches
# nothing (as this bogus one won't), not evidence of a broken run.
if(NOT output MATCHES "provider chain:")
    message(FATAL_ERROR "--explain did not complete normally, cannot conclude anything from this run:\nresult=${result}\n${output}${error}")
endif()

# Byte-for-byte equality is the whole check: the file was seeded above with
# leftover `@Invalid()` on all three migratable keys, so if
# migrateLegacySettings() ran on this path at all, at least one of those
# lines would now differ (two keys removed, one replaced with a marker).
# Not comparing mtimes: file(TIMESTAMP) here only has one-second resolution,
# so it could not reliably tell "rewritten with identical bytes" apart from
# "never touched" within a single fast test run anyway -- content equality
# is the strictly stronger and non-racy signal for what this test needs.
if(NOT _before_contents STREQUAL _after_contents)
    message(FATAL_ERROR
        "--explain modified the settings file content:\nbefore:\n${_before_contents}\nafter:\n${_after_contents}\ndaemon output:\n${output}${error}")
endif()
