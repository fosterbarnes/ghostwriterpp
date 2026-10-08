# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Core Test)

ecm_add_test(
    sessionrestoretest.cpp
    ../../src/bookmark.cpp
    ../../src/sessionrestore.h
    LINK_LIBRARIES Qt::Core Qt::Test
    TEST_NAME sessionrestoretest
)
set_target_properties(sessionrestoretest PROPERTIES WIN32_EXECUTABLE OFF)
