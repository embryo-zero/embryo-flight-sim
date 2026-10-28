// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class NasalGeoParseTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(NasalGeoParseTests);
    CPPUNIT_TEST(testDecimal);
    CPPUNIT_TEST(testHemispheres);
    CPPUNIT_TEST(testDegreesMinutesSeconds);
    CPPUNIT_TEST(testInvalid);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp();
    void tearDown();

    void testDecimal();
    void testHemispheres();
    void testDegreesMinutesSeconds();
    void testInvalid();
};
