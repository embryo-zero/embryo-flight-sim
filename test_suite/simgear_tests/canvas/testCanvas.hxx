// SPDX-FileCopyrightText: (C) 2020 James Turner
// SPDX-License-Identifier: GPL-2.0-or-later


#pragma once


#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>


// The unit tests.
class CanvasTests : public CppUnit::TestFixture
{
    // Set up the test suite.
    CPPUNIT_TEST_SUITE(CanvasTests);
    CPPUNIT_TEST(testCanvasBasic);
    CPPUNIT_TEST(testImagePixelOps);
    CPPUNIT_TEST(testImageLayoutItem);
    CPPUNIT_TEST_SUITE_END();

public:
    // Set up function for each test.
    void setUp();

    // Clean up after each test.
    void tearDown();

    // The tests.
    void testCanvasBasic();
    void testImagePixelOps();
    void testImageLayoutItem();
};
