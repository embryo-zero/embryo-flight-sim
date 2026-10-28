// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#include "testGeoParse.hxx"

#include "test_suite/FGTestApi/testGlobals.hxx"

#include <Main/globals.hxx>

void NasalGeoParseTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("geo-parse");

    globals->get_subsystem_mgr()->bind();
    globals->get_subsystem_mgr()->init();
    FGTestApi::setUp::initStandardNasal();
    globals->get_subsystem_mgr()->postinit();
}

void NasalGeoParseTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();
}

// shared Nasal helpers: expect text to parse to lat/lon, or to fail
static const char* helpers = R"(
    var expect = func(text, lat, lon) {
        var c = geo.parse(text);
        unitTest.assert(c != nil, "failed to parse: " ~ text);
        unitTest.assert(abs(c.lat() - lat) < 0.0001 and abs(c.lon() - lon) < 0.0001,
            "wrong result for '" ~ text ~ "': " ~ c.lat() ~ ", " ~ c.lon());
    };
    var expectNil = func(text) {
        unitTest.assert(geo.parse(text) == nil, "should not have parsed: " ~ text);
    };
)";

void NasalGeoParseTests::testDecimal()
{
    bool ok = FGTestApi::executeNasal(std::string(helpers) + R"(
        expect("51.5074, -0.1278", 51.5074, -0.1278);   # Google Maps
        expect("51.5074,-0.1278", 51.5074, -0.1278);
        expect("51.5074 -0.1278", 51.5074, -0.1278);
        expect("  51.5074 ; -0.1278  ", 51.5074, -0.1278);
        expect("-33.8688, 151.2093", -33.8688, 151.2093);
        expect("+37.6188, -122.3750", 37.6188, -122.375);
        expect("lat: 51.5074 lon: -0.1278", 51.5074, -0.1278);
        expect("51.5074, -0.1278, 35m", 51.5074, -0.1278); # trailing altitude is ignored
        expect("51.5074° , -0.1278°", 51.5074, -0.1278);
        expect("51.5074, \xE2\x88\x920.1278", 51.5074, -0.1278); # Unicode minus
    )");
    CPPUNIT_ASSERT(ok);
}

void NasalGeoParseTests::testHemispheres()
{
    bool ok = FGTestApi::executeNasal(std::string(helpers) + R"(
        expect("40.7128° N, 74.0060° W", 40.7128, -74.006);
        expect("33.8688S 151.2093E", -33.8688, 151.2093);
        expect("N51.5074 W0.1278", 51.5074, -0.1278);
        expect("N 51.5074, W 0.1278", 51.5074, -0.1278);
        expect("51.5074 north 0.1278 west", 51.5074, -0.1278);
        expect("51.5074n 0.1278w", 51.5074, -0.1278);
        # longitude first: the letters decide
        expect("W 0.1278 N 51.5074", 51.5074, -0.1278);
        expect("0.1278W, 51.5074N", 51.5074, -0.1278);
        # a letter on one value only decides the other
        expect("0.1278 W, 51.5074", 51.5074, -0.1278);
    )");
    CPPUNIT_ASSERT(ok);
}

void NasalGeoParseTests::testDegreesMinutesSeconds()
{
    bool ok = FGTestApi::executeNasal(std::string(helpers) + R"(
        expect("51°30'26.6\"N 0°07'40.1\"W", 51.507389, -0.127806);
        expect("51° 30' 26.6\" N, 0° 7' 40.1\" W", 51.507389, -0.127806);
        expect("51°30′26.6″N 0°07′40.1″W", 51.507389, -0.127806);  # typographic marks
        expect("N 51 30.443 W 000 07.668", 51.507383, -0.1278);    # degrees, decimal minutes
        expect("51 30 26.6 -0 07 40.1", 51.507389, 0.127806 * -1) ;
        expect("S 33 52 7.7 E 151 12 33.5", -33.868806, 151.209306);
    )");
    CPPUNIT_ASSERT(ok);
}

void NasalGeoParseTests::testInvalid()
{
    bool ok = FGTestApi::executeNasal(std::string(helpers) + R"(
        expectNil("");
        expectNil("hello");
        expectNil("51.5074");                 # only one value
        expectNil("91.0, 0.0");               # latitude out of range
        expectNil("45.0, 181.0");             # longitude out of range
        expectNil("51.5074 N, 0.1278 N");     # both latitudes
        expectNil("N 51 30.443 W");           # missing value
        expectNil("51 61 0, 0 0 0");          # minutes out of range
        unitTest.assert(geo.parse(nil) == nil);
    )");
    CPPUNIT_ASSERT(ok);
}
