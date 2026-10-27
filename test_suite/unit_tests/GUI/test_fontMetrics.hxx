/*
 * SPDX-FileComment: Unit tests for canvas.FontMetrics (Nasal) / pango::FontMetrics
 * SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class FontMetricsTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(FontMetricsTests);
    CPPUNIT_TEST(testBasicMeasure);
    CPPUNIT_TEST(testFontChange);
    CPPUNIT_TEST(testBadFont);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp();
    void tearDown();

    void testBasicMeasure();
    void testFontChange();
    void testBadFont();
};
