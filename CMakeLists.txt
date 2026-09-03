cmake_minimum_required(VERSION 3.20)

project(plasma-lyrics VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMDeprecationSettings)
include(ECMQmlModule)
include(ECMAddTests)
include(FeatureSummary)

# QtCore drives core/ and daemon/; Qml is for the compiled QML module the
# plasmoid reads snapshots through (the file:// trap, see DESIGN.md 2.3).
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Qml Sql)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Package)
find_package(Plasma REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLite3 REQUIRED IMPORTED_TARGET sqlite3)

ecm_set_disabled_deprecation_versions(QT 6.6 KF 6.0)

option(BUILD_DAEMON "Build the plasma-lyricsd daemon" ON)
option(BUILD_PLASMOID "Build the Plasma widget" ON)
# Compile-time provider extension points (DESIGN #23); netease is the only
# online source in the first version.
option(PROVIDER_NETEASE "Enable the netease provider" ON)
option(PROVIDER_LRCLIB "Enable the lrclib provider (placeholder)" OFF)

enable_testing()

add_subdirectory(core)

if(BUILD_DAEMON)
    add_subdirectory(daemon)
endif()

if(BUILD_PLASMOID)
    add_subdirectory(frontend)
endif()

add_subdirectory(providers)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
