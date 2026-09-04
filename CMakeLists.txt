cmake_minimum_required(VERSION 3.24)

project(plasma-lyrics VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(BUILD_DAEMON "Build plasma-lyricsd" ON)
option(BUILD_PLASMOID "Build the Plasma applet and QML module" ON)
option(BUILD_IMPORT_WAYLYRICS "Build the one-shot waylyrics importer" ON)
option(ENABLE_PROVIDER_NETEASE "Build the NetEase provider" ON)

if(BUILD_PLASMOID)
    find_package(ECM 6.0 REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    include(KDEInstallDirs6)
else()
    include(GNUInstallDirs)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Sql)

add_subdirectory(core)
add_subdirectory(providers)

if(BUILD_DAEMON)
    add_subdirectory(daemon)
endif()

if(BUILD_PLASMOID)
    add_subdirectory(frontend)
endif()

if(BUILD_IMPORT_WAYLYRICS)
    add_subdirectory(tools/import-waylyrics)
endif()

if(BUILD_DAEMON)
    install(FILES systemd/plasma-lyricsd.service
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)
    install(FILES systemd/90-plasma-lyricsd.preset
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user-preset)
endif()
