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
