# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

add_executable(versionprobe versionprobe.cpp)
set_target_properties(versionprobe PROPERTIES
    AUTOMOC OFF AUTORCC OFF AUTOUIC OFF WIN32_EXECUTABLE OFF)

if(MSVC)
    set(cmarkLibraries libcmark-gfm libcmark-gfm-extensions)
else()
    set(cmarkLibraries libcmark-gfm_static libcmark-gfm-extensions_static)
endif()

ecm_add_test(
    exporterfactorytest.cpp
    ../../src/export/exporterfactory.cpp
    ../../src/export/exporter.cpp
    ../../src/export/exportformat.cpp
    ../../src/export/commandlineexporter.cpp
    ../../src/export/cmarkgfmexporter.cpp
    ../../src/markdown/cmarkgfmapi.cpp
    ../../src/markdown/previeweditmetadata.cpp
    ../../src/markdown/markdownast.cpp
    ../../src/markdown/markdownnode.cpp
    ../../src/markdown/memoryarena.cpp
    LINK_LIBRARIES Qt::Core Qt::Gui Qt::Test ${cmarkLibraries}
    TEST_NAME exporterfactorytest
)
target_include_directories(exporterfactorytest PRIVATE
    ../../src
    ../..
    ${CMAKE_BINARY_DIR}/3rdparty/cmark-gfm/src
)
add_dependencies(exporterfactorytest versionprobe)
set_target_properties(exporterfactorytest PROPERTIES WIN32_EXECUTABLE OFF)

foreach(family IN ITEMS builtin pandoc multimarkdown missing)
    add_test(NAME exporterfactorytest_${family} COMMAND exporterfactorytest)
endforeach()
set_tests_properties(exporterfactorytest_builtin PROPERTIES
    ENVIRONMENT "GW_TEST_EXPORTER=cmark-gfm")
set_tests_properties(exporterfactorytest_pandoc PROPERTIES
    ENVIRONMENT "GW_TEST_EXPORTER=Pandoc Strict")
set_tests_properties(exporterfactorytest_multimarkdown PROPERTIES
    ENVIRONMENT "GW_TEST_EXPORTER=MultiMarkdown")
set_tests_properties(exporterfactorytest_missing PROPERTIES
    ENVIRONMENT "GW_TEST_EXPORTER=MultiMarkdown;GW_TEST_MISSING_TOOL=1")
