/*
 * SPDX-FileComment: Unit tests for FlightPlanModel
 * SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class FlightPlanModelTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(FlightPlanModelTests);
    CPPUNIT_TEST(testBasicData);
    CPPUNIT_TEST(testProcedures);
    CPPUNIT_TEST(testEdits);
    CPPUNIT_TEST(testProcedureEdits);
    CPPUNIT_TEST(testHidden);
    CPPUNIT_TEST(testMagnetic);
    CPPUNIT_TEST(testChangeFlightPlan);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp();
    void tearDown();

    void testBasicData();
    void testProcedures();
    void testEdits();
    void testProcedureEdits();
    void testHidden();
    void testMagnetic();
    void testChangeFlightPlan();
};
