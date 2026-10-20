

// SPDX-FileCopyrightText: 2019 James Turner
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestFixture.h>

#include <memory>

#include <simgear/props/props.hxx>

class SGGeod;
class GPS;

// The flight plan unit tests.
class GPSTests : public CppUnit::TestFixture
{
    // Set up the test suite.
    CPPUNIT_TEST_SUITE(GPSTests);
    CPPUNIT_TEST(testBasic);
    CPPUNIT_TEST(testNavRadioSlave);
    CPPUNIT_TEST(testTurnAnticipation);
    CPPUNIT_TEST(testOBSMode);
    CPPUNIT_TEST(testDirectTo);
    CPPUNIT_TEST(testLegMode);
    CPPUNIT_TEST(testDirectToLegOnFlightplan);
    CPPUNIT_TEST(testLongLeg);
    CPPUNIT_TEST(testLongLegWestbound);
    CPPUNIT_TEST(testOffsetFlight);
    CPPUNIT_TEST(testOverflightSequencing);
    CPPUNIT_TEST(testOffcourseSequencing);
    CPPUNIT_TEST(testLegIntercept);
    CPPUNIT_TEST(testDirectToLegOnFlightplanAndResumeBuiltin);
    CPPUNIT_TEST(testBuiltinRevertToOBSAtEnd);
    CPPUNIT_TEST(testRadialIntercept);
  //  CPPUNIT_TEST(testSWIFT8);
    CPPUNIT_TEST(testDMEIntercept);
    CPPUNIT_TEST(testFinalLegCourse);
    CPPUNIT_TEST(testCourseLegIntermediateWaypoint);
    CPPUNIT_TEST(testExceedFlyByMaxAngleTurn);
    CPPUNIT_TEST(testFlyOverMaxInterceptAngle);
    CPPUNIT_TEST(testDeleteActiveWaypointBeforeDiscontinuity);

    CPPUNIT_TEST_SUITE_END();

    void setPositionAndStabilise(GPS* gps, const SGGeod& g);

    GPS* setupStandardGPS(SGPropertyNode_ptr config = {},
                                          const std::string name = "gps", const int index = 0);
    void setupRouteManager();

public:
    // Set up function for each test.
    void setUp();

    // Clean up after each test.
    void tearDown();

    // The tests.
    void testBasic();
    void testNavRadioSlave();
    void testTurnAnticipation();
    void testOBSMode();
    void testDirectTo();
    void testLegMode();
    void testDirectToLegOnFlightplan();
    void testLongLeg();
    void testLongLegWestbound();
    void testOffsetFlight();
    void testOffcourseSequencing();
    void testOverflightSequencing();
    void testLegIntercept();
    void testDirectToLegOnFlightplanAndResumeBuiltin();
    void testBuiltinRevertToOBSAtEnd();
    void testRadialIntercept();
    void testSWIFT8();
    void testDMEIntercept();
    void testFinalLegCourse();
    void testCourseLegIntermediateWaypoint();
    void testExceedFlyByMaxAngleTurn();
    void testFlyOverMaxInterceptAngle();
    void testDeleteActiveWaypointBeforeDiscontinuity();
};
