cmake_minimum_required(VERSION 3.24)

project(plasma-lyrics VERSION 0.2.1 LANGUAGES CXX)

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

# Debian policy requires /usr/share/doc/<pkg>/copyright; installing it
# unconditionally also gives every other distribution the licence text.
install(FILES LICENSE
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}
        RENAME copyright)

# --- CPack: Debian package (Debian 13 / trixie) ----------------------------
# A cache variable, not a literal, because CI will add a second distro
# release later and that must not require a CMakeLists edit.
set(CPACK_DEBIAN_PACKAGE_RELEASE "1~deb13" CACHE STRING
    "Debian package release/revision appended after the version")

set(CPACK_PACKAGE_NAME "plasma-lyrics")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# Matches packaging/aur/PKGBUILD so the two packagings don't disagree.
set(CPACK_PACKAGE_CONTACT "swim <swim853279614@163.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native synchronized desktop lyrics widget for Plasma 6")
set(CPACK_PACKAGE_DESCRIPTION
    "A Plasma 6 applet that shows synchronized lyrics for whatever is playing
in the current session. It follows any MPRIS player, including the web
players exposed through plasma-browser-integration, fetches and caches
lyrics, and renders them on the desktop or in a panel with independent
appearance settings for each. Matching and timing are done by a systemd
user service, plasma-lyricsd.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/swim233/plasma-lyrics")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_STRIP_FILES ON)
# dpkg-shlibdeps only sees ELF linkage. The widget's QML imports are resolved
# at runtime by the QML engine and carry none, so every one of them has to be
# named here by hand -- a package that installs cleanly but renders a blank
# widget is the failure mode dpkg-shlibdeps can't catch. libqt6sql6-sqlite is
# the QSQLITE driver plugin; without it the lyric cache silently fails.
# plasma-workspace and libplasma are left unversioned: trixie ships
# libplasma 6.3.5 alongside plasma-workspace 6.3.6, so a matching version
# pin would be unsatisfiable.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "plasma-workspace, plasma-desktoptheme, libqt6sql6-sqlite, qml6-module-org-kde-kirigami, qml6-module-org-kde-ksvg, qml6-module-org-kde-kquickcontrols")

include(CPack)
