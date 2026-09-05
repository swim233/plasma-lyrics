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
    # Not CMAKE_INSTALL_LIBDIR/systemd: KDEInstallDirs6 resolves that to a
    # multiarch path (e.g. lib/x86_64-linux-gnu) on Debian, and systemd never
    # scans a multiarch systemd/user directory -- the package installs fine
    # and "systemctl --user start" then reports "Unit not found". Ask systemd
    # itself via pkg-config instead. Only trust its answer when it lands
    # inside CMAKE_INSTALL_PREFIX -- pkg-config reports an absolute /usr
    # path, and honouring it blindly would make a non-system prefix build
    # (e.g. -DCMAKE_INSTALL_PREFIX=$HOME/.local) write into /usr. Fall back
    # to <prefix>/lib/systemd/user otherwise, which also covers a build
    # container where pkg-config or the systemd .pc file is absent.
    set(_systemd_user_unit_dir "${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
    set(_systemd_user_preset_dir "${CMAKE_INSTALL_PREFIX}/lib/systemd/user-preset")

    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SYSTEMD QUIET systemd)
        if(SYSTEMD_FOUND)
            pkg_get_variable(_pc_unit_dir systemd systemduserunitdir)
            pkg_get_variable(_pc_preset_dir systemd systemduserpresetdir)

            if(_pc_unit_dir)
                cmake_path(IS_PREFIX CMAKE_INSTALL_PREFIX "${_pc_unit_dir}" NORMALIZE _pc_unit_in_prefix)
                if(_pc_unit_in_prefix)
                    set(_systemd_user_unit_dir "${_pc_unit_dir}")
                endif()
            endif()

            if(_pc_preset_dir)
                cmake_path(IS_PREFIX CMAKE_INSTALL_PREFIX "${_pc_preset_dir}" NORMALIZE _pc_preset_in_prefix)
                if(_pc_preset_in_prefix)
                    set(_systemd_user_preset_dir "${_pc_preset_dir}")
                endif()
            endif()
        endif()
    endif()

    install(FILES systemd/plasma-lyricsd.service
            DESTINATION ${_systemd_user_unit_dir})
    install(FILES systemd/90-plasma-lyricsd.preset
            DESTINATION ${_systemd_user_preset_dir})
endif()
