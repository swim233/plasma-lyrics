# metadata.json's KPlugin.Version is what Plasma shows the user (widget list,
# System Settings) and cannot be generated from project(VERSION) --
# plasma_install_package() takes the package directory as-is. Catch drift
# between the two at configure time instead of at release time.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package/metadata.json" _metadata_json)
string(JSON _metadata_version GET "${_metadata_json}" KPlugin Version)
if(NOT _metadata_version STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "package/metadata.json KPlugin.Version is ${_metadata_version} but "
        "project(VERSION) is ${PROJECT_VERSION} -- update metadata.json's "
        "Version field to match.")
endif()

plasma_install_package(package io.github.swim233.plasma-lyrics)
ki18n_install(translations)

if(BUILD_TESTING)
    # The appearance tests are plain QML, but they run through a QuickTest
    # binary rather than the qmltestrunner on PATH: that one is Qt 5 on Arch,
    # and it fails silently on Qt 6 QML.
    find_package(Qt6 6.6 REQUIRED COMPONENTS QuickTest)
    add_executable(tst_appearance autotests/qmltest.cpp)
    target_link_libraries(tst_appearance PRIVATE Qt6::QuickTest Qt6::Qml KF6::I18n KF6::I18nQml)
    target_compile_options(tst_appearance PRIVATE -Wno-missing-field-initializers)
    target_compile_definitions(tst_appearance PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/autotests")
    add_test(NAME tst_appearance COMMAND tst_appearance)
    # LC_ALL=C.UTF-8 stops Qt's own startup locale warning from reaching
    # the real journal via sd_journal_send() (see
    # core/tests/CMakeLists.txt for the full explanation).
    #
    # FAIL_REGULAR_EXPRESSION is the only thing that makes a QML binding loop
    # fail anything: it is reported as a warning, so the suite exits 0 with one
    # present and no assertion anywhere notices. One shipped in this applet's
    # config pages and was found by grepping this binary's stderr by hand,
    # never by a test. Deliberately not scoped to a known loop -- the value is
    # catching the ones nobody predicted. Pristine main emits none, so this
    # does not start out noisy. It lives here rather than in a message handler
    # inside qmltest.cpp because QTest installs its own handler for the
    # duration of the run and would swallow one installed around it.
    #
    # DEMONSTRATED: a loop reported from inside a test function, or while
    # loading any test file after the first -- QTest's handler is active, the
    # warning carries the "QWARN :" prefix and an UnknownTestFunc tag when no
    # test is current, it reaches the output ctest captures, and the regex
    # matches. In every case the bare binary still exits 0; only ctest fails.
    #
    # NOT COVERED: a loop raised while THE FIRST TEST FILE QuickTest LOADS is
    # being loaded -- tst_appearance.qml, which sorts ahead of
    # tst_fontpicker.qml and tst_fontrender.qml. QTestLog is
    # not active yet, so Qt's default handler applies, the warning carries no
    # "QWARN :" prefix, and under ctest stderr is a pipe -- sd_journal_send()
    # (see core/tests/CMakeLists.txt), invisible to the regex. Measured
    # directly by planting one there: ctest passes.
    #
    # The hole is exactly one file wide because QuickTest loads and runs each
    # file in sequence rather than loading all documents up front -- a probe
    # with a byte-identical body is caught in a file that sorts after and
    # escapes in one that sorts before, filename the only variable. Were all
    # documents loaded up front, every file's load-time scope would be
    # uncovered instead.
    #
    # It is empty today: tst_appearance.qml's top-level declarations inside
    # the TestCase are Component blocks that only a test function creates,
    # plus two binding-free QtObject stubs that emit nothing while loading.
    # Any other top-level item would open it, and adding a test file that
    # sorts earlier would move the uncovered region to that new file instead --
    # this is a property of load order, not of tst_appearance.qml.
    #
    # The prefix is the marker: a loop you can see with
    # QT_FORCE_STDERR_LOGGING=1 but which carries no "QWARN :" is one this
    # gate cannot catch.
    set_tests_properties(tst_appearance PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8"
        FAIL_REGULAR_EXPRESSION "Binding loop")

    # package/contents/config/main.xml is read by nothing else in this tree:
    # qmllint never opens it and tst_appearance instantiates the config pages
    # without a schema. KConfigLoader gives up silently at the first XML error
    # and every later <entry> vanishes, so this is the only gate between an
    # ill-formed comment and every panel key disappearing at runtime.
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    add_executable(tst_configschema autotests/tst_configschema.cpp)
    target_link_libraries(tst_configschema PRIVATE Qt6::Test)
    target_compile_definitions(tst_configschema PRIVATE
        PLASMOID_PACKAGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/package")
    add_test(NAME tst_configschema COMMAND tst_configschema)
    set_tests_properties(tst_configschema PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
endif()
