# SPDX-FileCopyrightText: 2022-2024 Megan Conkle <megan.conkle@kdemail.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
if (POLICY CMP0063)
    cmake_policy(SET CMP0063 NEW)
endif()
if (POLICY CMP0115)
    cmake_policy(SET CMP0115 NEW)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
            "Default build type: Release" FORCE)
endif()

if (NOT BUILD_TESTING)
    set (BUILD_TESTING OFF)
endif()

message("Generating makefile for ${CMAKE_BUILD_TYPE} build...")

# KF6 only
set(QT_MAJOR_VERSION 6)
set(QT_MIN_VERSION "6.5.0")
set(KF_MAJOR_VERSION 6)
set(KF_DEP_VERSION "6.0.0")
set(KF_MIN_VERSION "6.0.0")

# use C++20 like KF6 itself
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Fork app version (About dialog). Repo-root VERSION.txt is the single source of truth.
# Named VERSION.txt (not Version) so Windows MSVC does not shadow C++20 <version>
# when PROJECT_SOURCE_DIR is on the include path for 3rdparty/ headers.
set(_gw_version_file "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt")
if(NOT EXISTS "${_gw_version_file}")
    message(FATAL_ERROR "VERSION.txt missing: ${_gw_version_file}")
endif()
file(READ "${_gw_version_file}" GHOSTWRITERPP_APP_VERSION)
string(STRIP "${GHOSTWRITERPP_APP_VERSION}" GHOSTWRITERPP_APP_VERSION)
if(GHOSTWRITERPP_APP_VERSION STREQUAL "")
    message(FATAL_ERROR "VERSION.txt is empty: ${_gw_version_file}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gw_version_file}")

# KDE Application Version for project()/ECM (numeric only; packaging metadata)
set(RELEASE_SERVICE_VERSION_MAJOR "26")
set(RELEASE_SERVICE_VERSION_MINOR "07")
set(RELEASE_SERVICE_VERSION_MICRO "70")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")

project(ghostwriter VERSION ${RELEASE_SERVICE_VERSION} LANGUAGES CXX)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)


# Append to the module path so modules can be overridden from the command line.
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
remove_definitions(-DQT_NO_KEYWORDS)
remove_definitions(-DQT_NO_CAST_TO_ASCII)
remove_definitions(-DQT_NO_CAST_FROM_ASCII)
include(KDECMakeSettings)
include(ECMInstallIcons)
include(ECMAddAppIcon)
include(ECMAddTests)
include(ECMPoQmTools)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(FeatureSummary)

if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/poqm")
    ecm_install_po_files_as_qm(poqm)
endif()

if(WIN32)
    option(GHOSTWRITER_BUILD_MAN_PAGES "Generate man pages (KDocTools; needs xmllint on PATH for every compile step)" OFF)
else()
    option(GHOSTWRITER_BUILD_MAN_PAGES "Generate man pages (KDocTools)" ON)
endif()

if(GHOSTWRITER_BUILD_MAN_PAGES)
    find_package(KF${KF_MAJOR_VERSION}DocTools ${KF_MIN_VERSION})
    set_package_properties(KF${KF_MAJOR_VERSION}DocTools PROPERTIES DESCRIPTION
        "Tools to generate documentation"
        TYPE OPTIONAL
    )
    find_program(KDOCTOOLS_XMLLINT_EXECUTABLE NAMES xmllint)
    if(KF${KF_MAJOR_VERSION}DocTools_FOUND AND NOT KDOCTOOLS_XMLLINT_EXECUTABLE)
        set(KF${KF_MAJOR_VERSION}DocTools_FOUND FALSE)
        message(STATUS "KF${KF_MAJOR_VERSION}DocTools: xmllint not found; skipping man/handbook generation")
    endif()
endif()

set(CMARK_TESTS OFF)
# With Clang (not MSVC) on Windows, cmark-gfm uses OUTPUT_NAME "cmark-gfm" for the
# static library while the DLL import library has the same name, so Ninja fails with
# "multiple rules generate ... cmark-gfm.lib". src/CMakeLists.txt links the static
# cmark targets whenever not MSVC; avoid building the shared libraries in that case.
if(WIN32 AND NOT MSVC)
    set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
    set(CMARK_STATIC ON CACHE BOOL "" FORCE)
endif()
# Pure-C cmark targets must not inherit Qt AUTOGEN. KDECMakeSettings already
# set CMAKE_AUTOMOC/AUTORCC ON globally, so turn them off for this subdirectory
# only, then restore for the app.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)
add_subdirectory(3rdparty/cmark-gfm EXCLUDE_FROM_ALL)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
add_subdirectory(src)

if(KF${KF_MAJOR_VERSION}DocTools_FOUND)
    kdoctools_install(po)
    add_subdirectory(doc)
endif()

# Add icon files to the application's source files to have CMake bundle them in the executable.
set(ICONS_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons)

set(ICON_FILES
    ${ICONS_FOLDER}/sc-apps-ghostwriter.svg
    ${ICONS_FOLDER}/16-apps-ghostwriter.png
    ${ICONS_FOLDER}/22-apps-ghostwriter.png
    ${ICONS_FOLDER}/32-apps-ghostwriter.png
    ${ICONS_FOLDER}/64-apps-ghostwriter.png
    ${ICONS_FOLDER}/128-apps-ghostwriter.png
    ${ICONS_FOLDER}/256-apps-ghostwriter.png
)

if(WIN32)
    target_sources(ghostwriterpp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/ghostwriterpp.rc)
elseif(APPLE)
    ecm_add_app_icon(ghostwriterpp ICONS ${ICON_FILES} OUTFILE_BASENAME ghostwriterpp)
endif()

if(BUILD_TESTING)
   add_subdirectory(autotest)
endif()


# See https://cmake.org/cmake/help/v3.15/prop_tgt/MACOSX_BUNDLE_INFO_PLIST.html
if(APPLE)
    string(TIMESTAMP CURRENT_YEAR "%Y")

    set_property(
        TARGET ghostwriterpp
        PROPERTY MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/mac/MacOSBundleInfo.plist.in
    )

    # These are substituted by CMake into MacOSBundleInfo.plist.in.
    set(MACOSX_BUNDLE_DISPLAY_NAME "ghostwriter++")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.kde.ghostwriterpp")
    set(MACOSX_BUNDLE_BUNDLE_NAME "ghostwriter++")
    set(MACOSX_BUNDLE_INFO_STRING "ghostwriter++ - A Markdown Editor")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING "ghostwriter++ ${GHOSTWRITERPP_APP_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${GHOSTWRITERPP_APP_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "2014-${CURRENT_YEAR} the ghostwriter++ authors")
endif()

install(TARGETS ghostwriterpp ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

ecm_install_icons(
    ICONS ${ICON_FILES}
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

install(
    PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/resources/linux/org.kde.ghostwriterpp.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/linux/org.kde.ghostwriterpp.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/**/*.cpp src/**/*.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
# Pre-commit hook is a bash script; skip on Windows where hooks often have no bash in PATH
if (NOT WIN32)
    kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
endif()
