// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

// Tests of the Nasal module registration, load ordering and reloading
class NasalReloadTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(NasalReloadTests);
    CPPUNIT_TEST(testModulesRegistered);
    CPPUNIT_TEST(testReloadDirectoryModule);
    CPPUNIT_TEST(testReloadTopLevelModule);
    CPPUNIT_TEST(testReloadUnknownModule);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp();
    void tearDown();

    void testModulesRegistered();
    void testReloadDirectoryModule();
    void testReloadTopLevelModule();
    void testReloadUnknownModule();
};
