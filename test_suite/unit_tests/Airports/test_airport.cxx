/*
 * SPDX-FileCopyrightText: 2021 Keith Paterson
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "test_airport.hxx"

#include <iostream>
#include <cstring>
#include <memory>

#include "test_suite/FGTestApi/NavDataCache.hxx"
#include "test_suite/FGTestApi/TestDataLogger.hxx"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <simgear/math/sg_geodesy.hxx>
#include <simgear/math/SGGeod.hxx>
#include <AIModel/AIAircraft.hxx>
#include <AIModel/AIFlightPlan.hxx>
#include <AIModel/AIManager.hxx>
#include <AIModel/performancedb.hxx>

#include <Airports/airport.hxx>
#include <Airports/airportdynamicsmanager.hxx>
#include <Airports/runways.hxx>
#include <Traffic/TrafficMgr.hxx>
#include <Time/TimeManager.hxx>

#include <ATC/atc_mgr.hxx>

#include <Main/fg_props.hxx>
#include <Main/globals.hxx>

/////////////////////////////////////////////////////////////////////////////

// Set up function for each test.
void AirportTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("Airports");
    FGTestApi::setUp::initNavDataCache();

    globals->get_subsystem_mgr()->bind();
    globals->get_subsystem_mgr()->init();
    FGTestApi::setUp::initStandardNasal();
    globals->get_subsystem_mgr()->postinit();
}

// Clean up after each test.
void AirportTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();
}

/**
 * @brief Read an airport from the apt.dat
 *
 */
void AirportTests::testAirport()
{
    FGAirportRef departureAirport = FGAirport::getByIdent("YSSY");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Must have correct id", (std::string)"YSSY", departureAirport->getId());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Must have runways", (unsigned int) 6, departureAirport->numRunways());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Must have runway 16R", true, departureAirport->hasRunwayWithIdent("16R"));

    int length = 3962;

    FGRunwayRef runway = departureAirport->getRunwayByIdent("16R");
    int calculated = SGGeodesy::distanceM(runway->begin(), runway->end());
    CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE("Distance between the runway endpoints should be runway length", length, calculated, 1);
    calculated = SGGeodesy::distanceM(runway->begin(), runway->pointOnCenterline(-length));
    CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE("Distance between the runway start and point on centerline should be runway length", length, calculated, 1);

}

void AirportTests::testAirportNasal()
{
    bool ok = FGTestApi::executeNasal(R"(
        var apt = airportinfo('EGCC');
        var rwy = apt.runways['05R'];
        unitTest.assert_doubles_equal(rwy.heading, 51, 0.1);

        var mag = magvar(apt);
        unitTest.assert_doubles_equal(rwy.magnetic_heading, 51 - mag, 0.1);

        # Test findAirportsWithinRange using legacy ghost (airportinfo):
        var allAptsLegacy = positioned.findAirportsWithinRange(apt, 60);
        var filteredAptsLegacy = positioned.findAirportsWithinRange({
            pos: apt,
            range: 60,
            min_length_ft: 9000,
        });
        unitTest.assert(size(allAptsLegacy) > 0);
        unitTest.assert(size(filteredAptsLegacy) > 0);
        unitTest.assert(size(filteredAptsLegacy) <= size(allAptsLegacy));

        # Test findAirportsWithinRange using new cppbind ghost (positioned.airportinfo):
        var aptCppBind = positioned.airportinfo('EGCC');
        var allAptsCppBind = positioned.findAirportsWithinRange(aptCppBind, 60);
        var filteredAptsCppBind = positioned.findAirportsWithinRange({
            pos: aptCppBind,
            range: 60,
            min_length_ft: 9000,
        });
        unitTest.assert_equal(size(allAptsCppBind), size(allAptsLegacy));
        unitTest.assert_equal(size(filteredAptsCppBind), size(filteredAptsLegacy));

        # Test global legacy function findAirportsWithinRange with keyword/hash options:
        var legacyGlobal = findAirportsWithinRange({
            pos: apt,
            range: 60,
            type: "airport",
            min_length_ft: 9000,
        });
        unitTest.assert_equal(size(legacyGlobal), size(filteredAptsLegacy));

        # Test type filtering (e.g. heliports):
        var heliports = positioned.findAirportsWithinRange(apt, 60, "heliport");
        var heliportsHash = positioned.findAirportsWithinRange({
            pos: apt,
            range: 60,
            type: "heliport",
        });
        unitTest.assert(size(heliports) > 0);
        unitTest.assert_equal(size(heliports), size(heliportsHash));
        foreach (var h; heliports) {
            unitTest.assert_equal(h.type, "heliport");
        }

        # Test hard_surface = false returns all airports including unpaved (matches findWithinRange):
        var allAirportsWithUnpaved = positioned.findWithinRange(apt, 60, "airport");
        var aptsAnySurface = positioned.findAirportsWithinRange({
            pos: apt,
            range: 60,
            min_length_ft: 0,
            hard_surface: false,
        });
        unitTest.assert(size(allAirportsWithUnpaved) > 0);
        unitTest.assert_equal(size(aptsAnySurface), size(allAirportsWithUnpaved));
        unitTest.assert(size(aptsAnySurface) >= size(allAptsLegacy));
    )");
    CPPUNIT_ASSERT(ok);
}
