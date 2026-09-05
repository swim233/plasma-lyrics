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
    target_compile_definitions(tst_appearance PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/autotests")
    add_test(NAME tst_appearance COMMAND tst_appearance)
    set_tests_properties(tst_appearance PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
