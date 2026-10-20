// SPDX-FileCopyrightText: 2020 James Turner
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"


#include "flightplanTestHelpers.hxx"
#include "test_flightplan.hxx"

#include <algorithm>

#include "test_suite/FGTestApi/testGlobals.hxx"
#include "test_suite/FGTestApi/NavDataCache.hxx"

#include <simgear/misc/strutils.hxx>
#include <simgear/misc/sg_dir.hxx>
#include <simgear/io/iostreams/sgstream.hxx>
#include <simgear/structure/exception.hxx>
#include <simgear/magvar/magvar.hxx>
#include <simgear/timing/sg_time.hxx>

#include <Navaids/FlightPlan.hxx>
#include <Navaids/routePath.hxx>
#include <Navaids/NavDataCache.hxx>
#include <Navaids/waypoint.hxx>
#include <Navaids/navlist.hxx>
#include <Navaids/navrecord.hxx>
#include <Navaids/airways.hxx>
#include <Navaids/fix.hxx>

#include <Airports/airport.hxx>
#include <Main/globals.hxx>

using namespace std::string_literals;
using namespace flightgear;
using namespace FGTestApi::flightplan;

/////////////////////////////////////////////////////////////////////////////

namespace {
class TestFPDelegate : public FlightPlan::Delegate
{
public:
    TestFPDelegate(FlightPlan* plan) :
        _plan(plan)
    {}

    virtual ~TestFPDelegate()
    {
    }

    void sequence() override
    {
    }

    void currentWaypointChanged() override
    {
    }

    void waypointsChanged() override
    {
        sawWaypointsChange = true;
    }

    void departureChanged() override
    {
        sawDepartureChange = true;


        // mimic the default delegate, inserting the SID waypoints

        // clear anything existing
        _plan->clearWayptsWithFlag(WPT_DEPARTURE);

        // insert waypt for the dpearture runway
        auto dr = new RunwayWaypt(_plan->departureRunway(), _plan);
        dr->setFlag(WPT_DEPARTURE);
        dr->setFlag(WPT_GENERATED);
        _plan->insertWayptAtIndex(dr, 0);

        if (_plan->sid()) {
            WayptVec sidRoute;
            bool ok = _plan->sid()->route(_plan->departureRunway(), _plan->sidTransition(), sidRoute);
            if (!ok)
                throw sg_exception("failed to route via SID");
            int insertIndex = 1;
            for (auto w : sidRoute) {
                w->setFlag(WPT_DEPARTURE);
                w->setFlag(WPT_GENERATED);
                _plan->insertWayptAtIndex(w, insertIndex++);
            }
        }
    }

    void arrivalChanged() override
    {
        sawArrivalChange = true;

        // mimic the default delegate, inserting the STAR waypoints

        // clear anything existing
        _plan->clearWayptsWithFlag(WPT_ARRIVAL);
        _plan->clearWayptsWithFlag(flightgear::WPT_APPROACH);

        if (!_plan->destinationAirport()) {
            return;
        }

        if (!_plan->destinationRunway()) {
            auto ap = new NavaidWaypoint(_plan->destinationAirport(), _plan);
            ap->setFlag(WPT_ARRIVAL);
            ap->setFlag(WPT_GENERATED);
            _plan->insertWayptAtIndex(ap, -1);
            return;
        }

        // insert waypt for the destination runway
        auto dr = new RunwayWaypt(_plan->destinationRunway(), _plan);
        dr->setFlag(WPT_APPROACH);
        dr->setFlag(WPT_GENERATED);
        auto leg = _plan->insertWayptAtIndex(dr, -1);

        if (_plan->star()) {
            WayptVec starRoute;
            bool ok = _plan->star()->route(_plan->destinationRunway(), _plan->starTransition(), starRoute);
            if (!ok)
                throw sg_exception("failed to route via STAR");
            int insertIndex = leg->index();
            for (auto w : starRoute) {
                w->setFlag(WPT_ARRIVAL);
                w->setFlag(WPT_GENERATED);
                _plan->insertWayptAtIndex(w, insertIndex++);
            }
        }

        if (_plan->approach()) {
            WayptVec approachRoute;
            bool ok = _plan->approach()->routeFromVectors(approachRoute);
            if (!ok)
                throw sg_exception("failed to route approach");
            int insertIndex = leg->index();
            for (auto w : approachRoute) {
                w->setFlag(WPT_APPROACH);
                w->setFlag(WPT_GENERATED);
                _plan->insertWayptAtIndex(w, insertIndex++);
            }
        }
    }


    void loaded() override
    {
        didLoad = true;
    }

    FlightPlan* _plan = nullptr;
    bool didLoad = false;
    bool sawDepartureChange = false;
    bool sawArrivalChange = false;
    bool sawWaypointsChange = false;
};

class TestFPDelegateFactory : public FlightPlan::DelegateFactory
{
public:
    TestFPDelegateFactory() = default;
    virtual ~TestFPDelegateFactory() = default;

  FlightPlan::Delegate* createFlightPlanDelegate(FlightPlan* fp) override
  {
      auto d = new TestFPDelegate(fp);
      _instances.push_back(d);
      return d;
  }

    void destroyFlightPlanDelegate(FlightPlan* fp, FlightPlan::Delegate* d) override
    {
        auto it = std::find_if(_instances.begin(), _instances.end(), [d] (TestFPDelegate* fpd) {
            return fpd == d;
        });

        CPPUNIT_ASSERT(it != _instances.end());

        _instances.erase(it);
        delete d;
    }

    static TestFPDelegate* delegateForPlan(FlightPlan* fp);

    std::vector<TestFPDelegate*> _instances;
};

std::shared_ptr<TestFPDelegateFactory> static_factory;

TestFPDelegate* TestFPDelegateFactory::delegateForPlan(FlightPlan* fp)
{
    auto it = std::find_if(static_factory->_instances.begin(),
                           static_factory->_instances.end(),
                           [fp] (TestFPDelegate* del) {
        return del->_plan == fp;
    });

    if (it == static_factory->_instances.end())
        return nullptr;

    return *it;
}

} // of anonymous namespace

/////////////////////////////////////////////////////////////////////////////


// Set up function for each test.
void FlightplanTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("flightplan"s);
    FGTestApi::setUp::initNavDataCache();

    globals->get_subsystem_mgr()->bind();
    globals->get_subsystem_mgr()->init();
    globals->get_subsystem_mgr()->postinit();
}


// Clean up after each test.
void FlightplanTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();

    if (static_factory) {
        FlightPlan::unregisterDelegateFactory(static_factory);
        static_factory.reset();
    }
}

static FlightPlanRef makeTestFP(const std::string& depICAO, const std::string& depRunway,
                         const std::string& destICAO, const std::string& destRunway,
                         const std::string& waypoints, bool asRoute = false)
{
    FlightPlanRef f = asRoute ? FlightPlan::createRoute() : FlightPlan::create();
    FGTestApi::setUp::populateFPWithoutNasal(f, depICAO, depRunway, destICAO, destRunway, waypoints);
    return f;
}

void FlightplanTests::testBasic()
{
    FlightPlanRef fp1 = makeTestFP("EGCC"s, "23L"s, "EHAM"s, "24"s,
                                   "TNT CLN"s);
    fp1->setIdent("testplan"s);

    CPPUNIT_ASSERT(fp1->ident() == "testplan"s);
    CPPUNIT_ASSERT(fp1->departureAirport()->ident() == "EGCC"s);
    CPPUNIT_ASSERT(fp1->departureRunway()->ident() == "23L"s);
    CPPUNIT_ASSERT(fp1->destinationAirport()->ident() == "EHAM"s);
    CPPUNIT_ASSERT(fp1->destinationRunway()->ident() == "24"s);

    CPPUNIT_ASSERT_EQUAL(5, fp1->numLegs());

    CPPUNIT_ASSERT(fp1->legAtIndex(0)->waypoint()->source()->ident() == "23L"s);

    CPPUNIT_ASSERT(fp1->legAtIndex(1)->waypoint()->source()->ident() == "TNT"s);
    CPPUNIT_ASSERT(fp1->legAtIndex(1)->waypoint()->source()->name() == "TRENT VOR-DME"s);

    CPPUNIT_ASSERT(fp1->legAtIndex(2)->waypoint()->source()->ident() == "CLN"s);
    CPPUNIT_ASSERT(fp1->legAtIndex(2)->waypoint()->source()->name() == "CLACTON VOR-DME"s);

    CPPUNIT_ASSERT(fp1->legAtIndex(4)->waypoint()->source()->ident() == "24"s);

}

void FlightplanTests::testRoutePathBasic()
{
    FlightPlanRef fp1 = makeTestFP("EGHI"s, "20"s, "EDDM"s, "08L"s,
                                   "SFD LYD BNE CIV ELLX LUX SAA KRH WLD"s);


    RoutePath rtepath(fp1);
    const int legCount = fp1->numLegs();
    for (int leg = 0; leg < legCount; ++leg) {
        rtepath.trackForIndex(leg);
        rtepath.pathForIndex(leg);
        rtepath.distanceForIndex(leg);
    }

    rtepath.distanceBetweenIndices(2, 5);

    // check some leg parameters

    // BOLOUGNE SUR MER, near LFAY (AMIENS)
    FGNavRecordRef bne = FGNavList::findByFreq(113.8, FGAirport::getByIdent("LFAY"s)->geod());

    // CHIEVRES
    FGNavRecordRef civ = FGNavList::findByFreq(113.2, FGAirport::getByIdent("EBCI"s)->geod());

    double distM = SGGeodesy::distanceM(bne->geod(), civ->geod());
    double trackDeg = SGGeodesy::courseDeg(bne->geod(), civ->geod());

    CPPUNIT_ASSERT_DOUBLES_EQUAL(trackDeg, rtepath.trackForIndex(4), 0.5);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(distM, rtepath.distanceForIndex(4), 2000); // 2km precision, allow for turns

}

// https://sourceforge.net/p/flightgear/codetickets/1703/
// https://sourceforge.net/p/flightgear/codetickets/1939/

void FlightplanTests::testRoutePathSkipped()
{
    FlightPlanRef fp1 = makeTestFP("EHAM"s, "24"s, "EDDM"s, "08L"s,
                                   "EHEH KBO TAU FFM FFM/100/0.01 FFM/120/0.02 WUR WLD"s);


    RoutePath rtepath(fp1);

    // skipped point uses inbound track
    CPPUNIT_ASSERT_EQUAL(rtepath.trackForIndex(4), rtepath.trackForIndex(5));

    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, rtepath.distanceForIndex(5), 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, rtepath.distanceForIndex(6), 1e-9);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(101000, rtepath.distanceForIndex(7), 1000);


    // this tests skipping two preceding points works as it should
    SGGeodVec vec = rtepath.pathForIndex(7);
    CPPUNIT_ASSERT(vec.size() == 9);
}

void FlightplanTests::testRoutePathTrivialFlightPlan()
{
    FlightPlanRef fp1 = makeTestFP("EGPH"s, "24"s, "EGPH"s, "06"s,
                                   ""s);


    RoutePath rtepath(fp1);
    const int legCount = fp1->numLegs();
    for (int leg = 0; leg < legCount; ++leg) {
        rtepath.trackForIndex(leg);
        rtepath.pathForIndex(leg);
        rtepath.distanceForIndex(leg);
    }

    CPPUNIT_ASSERT_DOUBLES_EQUAL(19.5, fp1->totalDistanceNm(), 0.1);
}

void FlightplanTests::testRoutePathVec()
{
    FlightPlanRef fp1 = makeTestFP("KNUQ"s, "14L"s, "PHNL"s, "22R"s,
                                   "ROKME WOVAB"s);
    RoutePath rtepath(fp1);

    SGGeodVec vec = rtepath.pathForIndex(0);

    FGAirportRef ksfo = FGAirport::findByIdent("KSFO"s);
    FGFixRef rokme = fgpositioned_cast<FGFix>(FGPositioned::findClosestWithIdent("ROKME"s, ksfo->geod()));
    auto depRwy = fp1->departureRunway();

    CPPUNIT_ASSERT_DOUBLES_EQUAL(depRwy->geod().getLongitudeDeg(), vec.front().getLongitudeDeg(), 0.01);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(depRwy->geod().getLatitudeDeg(), vec.front().getLatitudeDeg(), 0.01);

    SGGeodVec vec1 = rtepath.pathForIndex(1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLongitudeDeg(), vec1.back().getLongitudeDeg(), 0.01);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLatitudeDeg(), vec1.back().getLatitudeDeg(), 0.01);

    SGGeodVec vec2 = rtepath.pathForIndex(2);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLongitudeDeg(), vec2.front().getLongitudeDeg(), 0.01);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLatitudeDeg(), vec2.front().getLatitudeDeg(), 0.01);

    //CPPUNIT_ASSERT(vec.front()
}

void FlightplanTests::testRoutePathFinalLegVQPR15()
{
    // test behaviour of RoutePath when the last leg prior to the arrival runway
    // is beyond the runway. This occurs in Paro RNAVZ15 approach.

    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    FlightPlanRef f = FlightPlan::create();
//    auto ourDelegate = TestFPDelegateFactory::delegateForPlan(f);

    auto vidp = FGAirport::findByIdent("VIDP"s);
    f->setDeparture(vidp->getRunwayByIdent("09"s));

    auto vqpr = FGAirport::findByIdent("VQPR"s);
    f->setDestination(vqpr->getRunwayByIdent("15"s));
    f->setApproach(vqpr->findApproachWithIdent("RNVZ15"s));
    RoutePath rtepath(f);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1100, rtepath.distanceForIndex(14), 100); // calculated from raw lat lon.
}

void FlightplanTests::testRoutPathWpt0Midflight()
{
    // test behaviour of RoutePath when WP0 is not a runway
    // happens for the Airbus NavDisplay which removes past wpts when sequencing

    FlightPlanRef fp1 = makeTestFP("KNUQ"s, "14L"s, "PHNL"s, "22R"s,
                                   "ROKME WOVAB"s);
    // actually delete leg 0 so we start at ROKME
    fp1->deleteIndex(0);

    RoutePath rtepath(fp1);

    SGGeodVec vec = rtepath.pathForIndex(0);

    FGAirportRef ksfo = FGAirport::findByIdent("KSFO"s);
    FGFixRef rokme = fgpositioned_cast<FGFix>(FGPositioned::findClosestWithIdent("ROKME"s, ksfo->geod()));

    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLongitudeDeg(), vec.front().getLongitudeDeg(), 0.01);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLatitudeDeg(), vec.front().getLatitudeDeg(), 0.01);

    SGGeodVec vec2 = rtepath.pathForIndex(1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLongitudeDeg(), vec2.front().getLongitudeDeg(), 0.01);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(rokme->geod().getLatitudeDeg(), vec2.front().getLatitudeDeg(), 0.01);

    //CPPUNIT_ASSERT(vec.front()
}

void FlightplanTests::testBasicAirways()
{
    Airway* awy = Airway::findByIdent("J547"s, Airway::HighLevel);
    CPPUNIT_ASSERT_EQUAL(awy->ident(), "J547"s);

    FGAirportRef kord = FGAirport::findByIdent("KORD"s);
    FlightPlanRef f = FlightPlan::create();
    f->setDeparture(kord);

    CPPUNIT_ASSERT(awy->findEnroute("KITOK"s));
    CPPUNIT_ASSERT(awy->findEnroute("LESUB"s));

    auto wpt = awy->findEnroute("FNT"s);
    CPPUNIT_ASSERT(wpt);
    CPPUNIT_ASSERT(wpt->ident() == "FNT"s);
    CPPUNIT_ASSERT(wpt->source() == FGNavRecord::findClosestWithIdent("FNT"s, kord->geod()));

    auto wptKUBBS = f->waypointFromString("KUBBS"s);
    auto wptFNT = f->waypointFromString("FNT"s);

    CPPUNIT_ASSERT(awy->canVia(wptKUBBS, wptFNT));

    WayptVec path = awy->via(wptKUBBS, wptFNT);
    CPPUNIT_ASSERT_EQUAL(4, static_cast<int>(path.size()));

    CPPUNIT_ASSERT_EQUAL("PMM"s, path.at(0)->ident());
    CPPUNIT_ASSERT_EQUAL("HASTE"s, path.at(1)->ident());
    CPPUNIT_ASSERT_EQUAL("DEWIT"s, path.at(2)->ident());
    CPPUNIT_ASSERT_EQUAL("FNT"s, path.at(3)->ident());

    f->insertWayptsAtIndex(path, 0);
    CPPUNIT_ASSERT_EQUAL(4, static_cast<int>(f->numLegs()));
    CPPUNIT_ASSERT_EQUAL("DEWIT"s, f->legAtIndex(2)->waypoint()->ident());
    CPPUNIT_ASSERT_EQUAL(static_cast<RouteBase*>(awy), f->legAtIndex(2)->waypoint()->owner());
}

void FlightplanTests::testViaInsertIntoFP()
{
    Airway* awy = Airway::findByIdent("J547"s, Airway::HighLevel);
    CPPUNIT_ASSERT_EQUAL(awy->ident(), "J547"s);

    FGAirportRef kord = FGAirport::findByIdent("KORD"s);
    FlightPlanRef f = FlightPlan::create();
    f->setDeparture(kord);

    CPPUNIT_ASSERT(awy->findEnroute("KITOK"s));
    CPPUNIT_ASSERT(awy->findEnroute("LESUB"s));

    auto wpt = awy->findEnroute("FNT"s);
    CPPUNIT_ASSERT(wpt);

    auto wptKUBBS = f->waypointFromString("KUBBS"s);
    auto wptFNT = f->waypointFromString("FNT"s);
    f->insertWayptAtIndex(wptKUBBS, -1);
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(f->numLegs()));

    Via* via = new Via(f, awy, wptFNT->source());
    f->insertWayptAtIndex(via, -1);
    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(f->numLegs()));
    CPPUNIT_ASSERT_EQUAL("PMM"s, f->legAtIndex(1)->waypoint()->ident());
    CPPUNIT_ASSERT_EQUAL(static_cast<RouteBase*>(awy), f->legAtIndex(1)->waypoint()->owner());
}

void FlightplanTests::testViaInsertIntoRoute()
{
    Airway* awy = Airway::findByIdent("J547"s, Airway::HighLevel);
    CPPUNIT_ASSERT_EQUAL(awy->ident(), "J547"s);

    FGAirportRef kord = FGAirport::findByIdent("KORD"s);
    FlightPlanRef f = FlightPlan::createRoute();
    f->setDeparture(kord);

    CPPUNIT_ASSERT(awy->findEnroute("KITOK"s));
    CPPUNIT_ASSERT(awy->findEnroute("LESUB"s));

    auto wpt = awy->findEnroute("FNT"s);
    CPPUNIT_ASSERT(wpt);

    auto wptKUBBS = f->waypointFromString("KUBBS"s);
    auto wptFNT = f->waypointFromString("FNT"s);
    f->insertWayptAtIndex(wptKUBBS, -1);
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(f->numLegs()));

    Via* via = new Via(f, awy, wptFNT->source());
    f->insertWayptAtIndex(via, -1);
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(f->numLegs()));

// clone and convert to active flight-plan; Vias
// should be expanded
    FlightPlanRef activeFP = f->clone("ACT", true);

    CPPUNIT_ASSERT_EQUAL(5, static_cast<int>(activeFP->numLegs()));
    CPPUNIT_ASSERT_EQUAL("PMM"s, activeFP->legAtIndex(1)->waypoint()->ident());
    CPPUNIT_ASSERT_EQUAL(static_cast<RouteBase*>(awy), activeFP->legAtIndex(1)->waypoint()->owner());
}

void FlightplanTests::testAirwayNetworkRoute()
{
    FGAirportRef egph = FGAirport::findByIdent("EGPH"s);
    FlightPlanRef f = FlightPlan::create();
    f->setDeparture(egph);

    auto highLevelNet = Airway::highLevel();

    auto wptTLA = f->waypointFromString("TLA"s);
    auto wptCNA = f->waypointFromString("CNA"s); // codespell:ignore cna

    WayptVec route;
    bool ok = highLevelNet->route(wptTLA, wptCNA, route);
    CPPUNIT_ASSERT(ok);

    CPPUNIT_ASSERT_EQUAL(static_cast<int>(route.size()), 18);
}

void FlightplanTests::testParseICAORoute()
{
    FGAirportRef kord = FGAirport::findByIdent("KORD"s);
    FlightPlanRef f = FlightPlan::create();
    f->setDeparture(kord);
    f->setDestination(FGAirport::findByIdent("KSAN"s));

    const char* route = "DCT JOT J26 IRK J96 SLN J18 GCK J96 CIM J134 GUP J96 KEYKE J134 DRK J78 LANCY J96 PKE";
  //  const char* route = "DCT KUBBS J547 FNT Q824 HOCKE Q905 SIKBO Q907 MIILS N400A TUDEP NATW GISTI DCT SLANY UL9 DIKAS UL18 GAVGO UL9 KONAN UL607 UBIDU Y863 RUDUS T109 HAREM T104 ANORA STAR";
    bool ok = f->parseICAORouteString(route);
    CPPUNIT_ASSERT(ok);



}

void FlightplanTests::testParseICANLowLevelRoute()
{
    const char* route = "DCT DPA V6 IOW V216 LAA V210 GOSIP V83 ACHES V210 BLOKE V83 ALS V210 RSK V95 INW V12 HOXOL V264 OATES V12 JUWSO V264 PKE"; // codespell:ignore als

    FGAirportRef kord = FGAirport::findByIdent("KORD"s);
    FlightPlanRef f = FlightPlan::create();
    f->setDeparture(kord);
    f->setDestination(FGAirport::findByIdent("KSAN"s));

    bool ok = f->parseICAORouteString(route);
    CPPUNIT_ASSERT(ok);
}

// https://sourceforge.net/p/flightgear/codetickets/1814/
void FlightplanTests::testBug1814()
{
    const std::string fpXML = R"(<?xml version="1.0" encoding="UTF-8"?>
                    <PropertyList>
                        <version type="int">2</version>
                        <departure>
                            <airport type="string">SAWG</airport>
                            <runway type="string">25</runway>
                        </departure>
                        <destination>
                            <airport type="string">SUMU</airport>
                        </destination>
                        <route>
                              <wp n="6">
                                    <type type="string">navaid</type>
                                    <ident type="string">PUGLI</ident>
                                    <lon type="double">-60.552200</lon>
                                    <lat type="double">-40.490000</lat>
                                  </wp>
                            <wp n="7">
                                <type type="string">navaid</type>
                                <ident type="string">SIGUL</ident>
                                <lon type="double">-59.655800</lon>
                                <lat type="double">-38.312800</lat>
                            </wp>
                            <wp n="8">
                                <type type="string">navaid</type>
                                <ident type="string">MDP</ident>
                                <lon type="double">-57.576400</lon>
                                <lat type="double">-37.929800</lat>
                            </wp>
                        </route>
                    </PropertyList>
  )";

    std::istringstream stream(fpXML);
    FlightPlanRef f = FlightPlan::create();
    bool ok = f->load(stream);
    CPPUNIT_ASSERT(ok);

    auto leg = f->legAtIndex(1);
    CPPUNIT_ASSERT_EQUAL("SIGUL"s, leg->waypoint()->ident());

    CPPUNIT_ASSERT_DOUBLES_EQUAL(137, leg->distanceNm(), 0.5);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(101, f->legAtIndex(2)->distanceNm(), 0.5);
}

void FlightplanTests::testLoadSaveMachRestriction()
{
    const std::string fpXML = R"(<?xml version="1.0" encoding="UTF-8"?>
       <PropertyList>
           <version type="int">2</version>
           <departure>
               <airport type="string">SAWG</airport>
               <runway type="string">25</runway>
           </departure>
           <destination>
               <airport type="string">SUMU</airport>
           </destination>
           <route>
                <wp n="0">
                  <type type="string">navaid</type>
                  <ident type="string">PUGLI</ident>
                  <lon type="double">-60.552200</lon>
                  <lat type="double">-40.490000</lat>
                </wp>
                <wp n="1">
                  <type type="string">basic</type>
                  <alt-restrict type="string">at</alt-restrict>
                  <altitude-ft type="double">36000</altitude-ft>
                  <speed-restrict type="string">mach</speed-restrict>
                  <speed type="double">1.24</speed>
                  <ident type="string">SV002</ident>
                  <lon type="double">-115.50531</lon>
                  <lat type="double">37.89523</lat>
                </wp>
                <wp n="2">
                     <type type="string">navaid</type>
                     <ident type="string">SIGUL</ident>
                     <lon type="double">-60.552200</lon>
                     <lat type="double">-40.490000</lat>
                   </wp>
           </route>
       </PropertyList>
     )";

     std::istringstream stream(fpXML);
    FlightPlanRef f = FlightPlan::create();
     bool ok = f->load(stream);
     CPPUNIT_ASSERT(ok);

     auto leg = f->legAtIndex(1);
    CPPUNIT_ASSERT_EQUAL(SPEED_RESTRICT_MACH, leg->speedRestriction());
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.24, leg->speedMach(), 0.01);

    auto firstLeg = f->legAtIndex(0);
    firstLeg->setSpeed(SPEED_RESTRICT_MACH, 1.56);

    // upgrade to a hold and set the count
    f->legAtIndex(2)->setHoldCount(8);

    // round trip through XML to check :)
    std::ostringstream ss;
    f->save(ss);

    std::istringstream iss(ss.str());
    FlightPlanRef f2 = FlightPlan::create();
    ok = f2->load(iss);
    CPPUNIT_ASSERT(ok);

    auto leg3 = f2->legAtIndex(0);
    CPPUNIT_ASSERT_EQUAL(SPEED_RESTRICT_MACH, leg3->speedRestriction());
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.56, leg3->speedMach(), 0.01);

    CPPUNIT_ASSERT_EQUAL(8, f2->legAtIndex(2)->holdCount());
}

void FlightplanTests::testLoadSaveBetweenRestriction()
{
    const std::string fpXML = R"(<?xml version="1.0" encoding="UTF-8"?>
       <PropertyList>
           <version type="int">2</version>
           <departure>
               <airport type="string">SAWG</airport>
               <runway type="string">25</runway>
           </departure>
           <destination>
               <airport type="string">SUMU</airport>
           </destination>
           <route>
                <wp n="0">
                  <type type="string">navaid</type>
                  <ident type="string">PUGLI</ident>
                  <lon type="double">-60.552200</lon>
                  <lat type="double">-40.490000</lat>
                </wp>
                <wp n="1">
                  <type type="string">basic</type>
                  <alt-restrict type="string">between</alt-restrict>
                  <altitude-ft type="double">36000</altitude-ft>
                  <constraint-altitude type="double">32000</constraint-altitude>
                  <ident type="string">SV002</ident>
                  <lon type="double">-115.50531</lon>
                  <lat type="double">37.89523</lat>
                </wp>
                <wp n="2">
                     <type type="string">navaid</type>
                     <ident type="string">SIGUL</ident>
                     <lon type="double">-60.552200</lon>
                     <lat type="double">-40.490000</lat>
                   </wp>
           </route>
       </PropertyList>
     )";

     std::istringstream stream(fpXML);
    FlightPlanRef f = FlightPlan::create();
     bool ok = f->load(stream);
     CPPUNIT_ASSERT(ok);

     auto leg = f->legAtIndex(1);
    CPPUNIT_ASSERT_EQUAL(RESTRICT_BETWEEN, leg->altitudeRestriction());
    CPPUNIT_ASSERT_DOUBLES_EQUAL(36000, leg->altitudeFt(), 0.01);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(32000, leg->waypoint()->constraintAltitude(ALTITUDE_FEET), 0.01);
}

void FlightplanTests::testRestrictionUnits()
{
    FlightPlanRef fp1 = makeTestFP("LIRF"s, "34L"s, "LEBL"s, "25R"s,
                                   "ESINO GITRI BALEN MUREN TOSNU"s);
    auto leg = fp1->legAtIndex(2);
    leg->setAltitude(RESTRICT_ABOVE, 120, ALTITUDE_FLIGHTLEVEL);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->altitude(), 12000.0, 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->altitude(ALTITUDE_METER), 12000.0 * SG_FEET_TO_METER, 0.1);

    leg->setSpeed(RESTRICT_AT, 300);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->speedKts(), 300, 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->speed(), 300, 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->speed(SPEED_KPH), 300 * SG_KT_TO_MPS * SG_MPS_TO_KMH, 0.1);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->speedMach(), 0.473, 0.1);

    // setting a mach restriction assumes Mach units by default
    leg->setSpeed(SPEED_RESTRICT_MACH, 0.7);
    leg->setAltitude(RESTRICT_AT, 38000);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(leg->speedKts(), 401, 1);
}

void FlightplanTests::testBasicDiscontinuity()
{
    FlightPlanRef fp1 = makeTestFP("LIRF"s, "34L"s, "LEBL"s, "25R"s,
                                   "ESINO GITRI BALEN MUREN TOSNU"s);

    const auto tdBefore = fp1->totalDistanceNm();


    const SGGeod balenPos = fp1->legAtIndex(3)->waypoint()->position();
    const SGGeod murenPos = fp1->legAtIndex(4)->waypoint()->position();
    const auto crs = SGGeodesy::courseDeg(balenPos, murenPos);

    // total distance should not change
    fp1->insertWayptAtIndex(new Discontinuity(fp1), 4); // between BALEN and MUREN

    CPPUNIT_ASSERT_DOUBLES_EQUAL(tdBefore, fp1->totalDistanceNm(), 1.0);


    CPPUNIT_ASSERT_DOUBLES_EQUAL(fp1->legAtIndex(4)->courseDeg(),
                                 SGGeodesy::courseDeg(balenPos, murenPos), 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(fp1->legAtIndex(4)->distanceNm(), 0.0, 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(fp1->legAtIndex(5)->courseDeg(),
                                 SGGeodesy::courseDeg(balenPos, murenPos), 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(fp1->legAtIndex(5)->distanceNm(),
                                 SGGeodesy::distanceNm(balenPos, murenPos), 0.1);

    // ensure that pointAlongRoute works correctly going into the DISCON


    const auto pos1 = fp1->pointAlongRoute(3, 20.0);
    const auto validP1 = SGGeodesy::direct(balenPos, crs, 20.0 * SG_NM_TO_METER);

    CPPUNIT_ASSERT(SGGeodesy::distanceM(pos1, validP1) < 500.0);

    const auto pos2 = fp1->pointAlongRoute(5, -10.0);
    const auto crs2 = SGGeodesy::courseDeg(murenPos, balenPos);
    const auto validP2 = SGGeodesy::direct(murenPos, crs2, 10.0 * SG_NM_TO_METER);

    CPPUNIT_ASSERT(SGGeodesy::distanceM(pos2, validP2) < 500.0);

    // remove the discontinuity
    fp1->deleteIndex(4);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(fp1->legAtIndex(4)->courseDeg(),
                                 SGGeodesy::courseDeg(balenPos, murenPos), 0.1);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(fp1->legAtIndex(4)->distanceNm(),
                                 SGGeodesy::distanceNm(balenPos, murenPos), 0.1);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(tdBefore, fp1->totalDistanceNm(), 1.0);

}

// https://gitlab.com/flightgear/flightgear/-/work_items/3550
void FlightplanTests::testDeleteActiveWaypointBeforeDiscontinuity()
{
    FlightPlanRef fp1 = makeTestFP("LIRF"s, "34L"s, "LEBL"s, "25R"s,
                                   "ESINO GITRI BALEN MUREN TOSNU"s);

    fp1->insertWayptAtIndex(new Discontinuity(fp1), 4); // between BALEN and MUREN
    CPPUNIT_ASSERT(fp1->legAtIndex(3)->waypoint()->ident() == "BALEN");
    CPPUNIT_ASSERT(fp1->legAtIndex(4)->waypoint()->type() == "discontinuity");

    // activate, and make BALEN (the WP before the discontinuity) current
    fp1->setCurrentIndex(3);
    CPPUNIT_ASSERT(fp1->isActive());
    CPPUNIT_ASSERT_EQUAL(3, fp1->currentIndex());

    // delete the active waypoint; the discontinuity becomes the current leg
    fp1->deleteIndex(3);

    CPPUNIT_ASSERT_EQUAL(3, fp1->currentIndex());
    CPPUNIT_ASSERT(fp1->currentLeg()->waypoint()->type() == "discontinuity");

    // exercise the things a delegate / the route manager would touch
    // on the new current leg; the bug report indicates an exception here
    auto leg = fp1->currentLeg();
    CPPUNIT_ASSERT_NO_THROW(leg->courseDeg());
    CPPUNIT_ASSERT_NO_THROW(leg->distanceNm());
    CPPUNIT_ASSERT_NO_THROW(fp1->totalDistanceNm());
    CPPUNIT_ASSERT_NO_THROW(fp1->nextLeg());
    CPPUNIT_ASSERT_NO_THROW(fp1->previousLeg());
    CPPUNIT_ASSERT_NO_THROW(fp1->sequence());
}

void FlightplanTests::testOnlyDiscontinuityRoute()
{
    FlightPlanRef f = FlightPlan::create();
    FGAirportRef depApt = FGAirport::getByIdent("LFBD"s);
    f->setDeparture(depApt);


    FGAirportRef destApt = FGAirport::getByIdent("EHAM"s);
    f->setDestination(destApt);

    f->insertWayptAtIndex(new Discontinuity(f), 0);

    RoutePath rp1(f);

    // discontinuity should act like a straight segment between preceding and following
    //const double d = SGGeodesy::distanceNm(depApt->geod(), destApt->geod());
    // CPPUNIT_ASSERT_DOUBLES_EQUAL(rp1.distanceForIndex(0), d, 0.5);

    // start inserting waypoints ahead of the DISCON, Boeing FPL style
    WayptRef wpt = f->waypointFromString("LMG"s);
    f->insertWayptAtIndex(wpt, 0);

    wpt = f->waypointFromString("KUKOR"s);
    f->insertWayptAtIndex(wpt, 1);

    wpt = f->waypointFromString("EPL"s);
    f->insertWayptAtIndex(wpt, 2);
}

void FlightplanTests::testLeadingWPDynamic()
{
    FlightPlanRef f = FlightPlan::create();
    // plan has no departure, so this discon is floating
    f->insertWayptAtIndex(new Discontinuity(f), 0);

    auto ha = new HeadingToAltitude(f, "TO_3000"s, 90);
    ha->setAltitude(3000, RESTRICT_AT);
    f->insertWayptAtIndex(ha, 1);

    RoutePath rp1(f);
    // distance will be invalid, but shouldn;t assert or crash :)
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f->totalDistanceNm(), 0.001);
}

static double magVarFor(const SGGeod& geod)
{
  double jd = globals->get_time_params()->getJD();
  return sgGetMagVar(geod, jd) * SG_RADIANS_TO_DEGREES;
}

void FlightplanTests::testRadialIntercept()
{
    // replicate AJO1R departure
    FlightPlanRef f = makeTestFP("LFKC"s, "36"s, "LIRF"s, "25"s, "BUNAX BEBEV AJO"s);

    // set BUNAX as overflight
    f->legAtIndex(1)->waypoint()->setFlag(WPT_OVERFLIGHT);
    f->insertWayptAtIndex(new BasicWaypt(SGGeod::fromDeg(8.78333, 42.566), "KC502"s, f), 1);

    SGGeod pos = SGGeod::fromDeg(8.445556,42.216944);
    auto intc = new RadialIntercept(f, "INTC"s, pos, 230, 5);
    f->insertWayptAtIndex(intc, 3); // between BUNAX and BEBEV

    RoutePath rtepath(f);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(230 + magVarFor(pos), f->legAtIndex(3)->courseDeg(), 1.0);
}

// every leg of the route has a sane length, and building the path didn't throw
static void checkRoutePathDistances(FlightPlanRef f, double maxLegNm)
{
    RoutePath path(f);
    for (int i = 0; i < f->numLegs(); ++i) {
        const double nm = path.distanceForIndex(i) * SG_METER_TO_NM;
        CPPUNIT_ASSERT_MESSAGE("leg " + std::to_string(i) + " " + f->legAtIndex(i)->waypoint()->ident() +
                                   " is " + std::to_string(nm) + " NM",
                               nm < maxLegNm);
    }
}

// codespell:ignore-begin FRAMS is a waypoint
void FlightplanTests::testRadialInterceptFromRadialOrigin()
{
    // KSMX BUELT4 departure, runway 30, FRAMS transition (FAA CIFP 2610): after
    // BUELT, fly heading 280 to intercept the BUELT-GLJ track, i.e. the radial
    // from the waypoint just reached. The intercept used to be computed tens of
    // thousands of km away, and the next leg's geodesic threw.
    FlightPlanRef f = makeTestFP("KSMX"s, "30"s, "KSBA"s, "25"s, ""s);
    const SGGeod buelt = SGGeod::fromDeg(-120.277197, 34.652511);
    WayptVec wps = {
        hdgToAlt(f, "(661)"s, 301.0, 661),
        new RadialIntercept(f, "GLJR132"s, SGGeod::fromDeg(-120.521481, 34.952375), 190.0, 132.0),
        fix(f, "BUELT"s, 34.652511, -120.277197),
        new RadialIntercept(f, "BUELTR312"s, buelt, 280.0, 312.2),
        fix(f, "GLJ"s, 34.952375, -120.521481),
        fix(f, "PISMO"s, 35.262556, -121.045747),
        fix(f, "FRAMS"s, 35.367206, -120.939422)};

    CPPUNIT_ASSERT_NO_THROW(f->insertWayptsAtIndex(wps, 1));
    checkRoutePathDistances(f, 100.0);

    // already on the radial at its origin: the intercept is immediate
    RoutePath path(f);
    const double nm = SGGeodesy::distanceNm(path.positionForIndex(4), buelt);
    CPPUNIT_ASSERT_MESSAGE("intercept " + std::to_string(nm) + " NM from BUELT", nm < 1.0);
}
// codespell:ignore-end

void FlightplanTests::testRadialInterceptNearStation()
{
    // KMTJ MTJ2 departure, runway 13 (FAA CIFP 2610): climb on 127, then
    // heading 340 to intercept MTJ R297, with MTJ beside the runway.
    FlightPlanRef f = makeTestFP("KMTJ"s, "13"s, "KGJT"s, "11"s, ""s);
    const SGGeod mtj = SGGeod::fromDeg(-107.899289, 38.506436);
    auto hold = new Hold(mtj, "MTJ"s, f);
    hold->setHoldRadial(116.6);
    hold->setRightHanded();
    hold->setHoldTime(60.0);
    WayptVec wps = {
        hdgToAlt(f, "(6159)"s, 127.0, 6159),
        new RadialIntercept(f, "MTJR297"s, mtj, 340.0, 297.0),
        hdgToAlt(f, "(8100)"s, 297.0, 8100),
        fix(f, "MTJ"s, 38.506436, -107.899289),
        hold};

    CPPUNIT_ASSERT_NO_THROW(f->insertWayptsAtIndex(wps, 1));
    checkRoutePathDistances(f, 100.0);
}

void FlightplanTests::testRadialInterceptMissedFromThreshold()
{
    // KAIK ILS07 missed approach (FAA CIFP 2610): from the threshold climb on
    // 066 to 1100, then heading 276 to intercept the RW07-VIXLY track, the
    // 246 radial from the threshold behind the aircraft.
    FlightPlanRef f = makeTestFP("KCAE"s, "29"s, "KAGS"s, "35"s, ""s);
    const SGGeod rw07 = SGGeod::fromDeg(-81.695781, 33.646072);
    WayptVec wps = {
        fix(f, "PEPCI"s, 33.606064, -81.779650),
        fix(f, "RW07"s, 33.646072, -81.695781),
        hdgToAlt(f, "(1100)"s, 66.3, 1100),
        new RadialIntercept(f, "RW07R246"s, rw07, 276.0, 246.3),
        fix(f, "VIXLY"s, 33.550289, -81.896242)};

    CPPUNIT_ASSERT_NO_THROW(f->insertWayptsAtIndex(wps, 1));
    checkRoutePathDistances(f, 100.0);
}

void FlightplanTests::testRadialIntersectionFarCrossing()
{
    // KMTJ MTJ2 runway 13, as computed live: the climb ends just south-east of
    // MTJ, and heading 340 only meets the line of R297 behind the station. The
    // formula's other crossing is MTJ's antipode, which must not be returned.
    const SGGeoc climbEnd = SGGeoc::fromRadM(-1.88227719363395, 0.67187008089714351, SG_EQUATORIAL_RADIUS_M);
    const SGGeoc mtj = SGGeoc::fromGeod(SGGeod::fromDeg(-107.899289, 38.506436));
    const double mv = magVarFor(SGGeod::fromGeoc(climbEnd));

    SGGeoc result;
    if (SGGeodesy::radialIntersection(climbEnd, 340.0 + mv, mtj, 297.0 + mv, result)) {
        const double nm = SGGeodesy::distanceNm(SGGeod::fromGeoc(result), SGGeod::fromGeoc(mtj));
        CPPUNIT_ASSERT_MESSAGE("intersection " + std::to_string(nm) + " NM from MTJ", nm < 100.0);
    }

    // starting at the radial's origin: the intercept is immediate
    CPPUNIT_ASSERT(SGGeodesy::radialIntersection(mtj, 280.0, mtj, 312.0, result));
    CPPUNIT_ASSERT(SGGeodesy::distanceM(SGGeod::fromGeoc(result), SGGeod::fromGeoc(mtj)) < 1.0);
}

// Throws from waypointsChanged() the first time only.
class ThrowingFPDelegate : public FlightPlan::Delegate
{
public:
    void waypointsChanged() override
    {
        ++calls;
        if (calls == 1) {
            throw sg_exception("test: delegate failure");
        }
    }

    int calls = 0;
};

void FlightplanTests::testDelegateExceptionReleasesLock()
{
    // An exception escaping a delegate (or rebuilding the leg data) must not
    // leave the plan's delegates locked: later changes must still run them.
    FlightPlanRef f = makeTestFP("EGCC"s, "23L"s, "EHAM"s, "24"s, "TNT"s);
    auto d = new ThrowingFPDelegate;
    f->addDelegate(d);

    CPPUNIT_ASSERT_THROW(f->insertWayptAtIndex(fix(f, "ONE"s, 53.0, -1.0), 1), sg_exception);
    CPPUNIT_ASSERT_EQUAL(1, d->calls);

    f->insertWayptAtIndex(fix(f, "TWO"s, 53.1, -1.1), 1);
    CPPUNIT_ASSERT_EQUAL(2, d->calls);
    CPPUNIT_ASSERT_EQUAL(std::string("TWO"), f->legAtIndex(1)->waypoint()->ident());

    f->removeDelegate(d);
    delete d;
}

void FlightplanTests::loadFGFPWithoutDepartureArrival()
{
    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    FlightPlanRef f = FlightPlan::create();

    SGPath fgfpPath = simgear::Dir::current().path() / "test_fgfp_without_dep_arr.fgfp"s;
    {
        sg_ofstream s(fgfpPath);
        s << R"(<?xml version="1.0" encoding="UTF-8"?>
            <PropertyList>
              <version type="int">2</version>
              <departure>
                <airport type="string">EDDM</airport>
                <runway type="string">08R</runway>
              </departure>
              <destination>
                <airport type="string">EDDF</airport>
              </destination>
              <route>
                <wp n="1">
                  <type type="string">navaid</type>
                  <ident type="string">GIVMI</ident>
                  <lon type="double">11.364700</lon>
                  <lat type="double">48.701100</lat>
                </wp>
                <wp n="2">
                  <type type="string">navaid</type>
                  <ident type="string">ERNAS</ident>
                  <lon type="double">11.219400</lon>
                  <lat type="double">48.844700</lat>
                </wp>
                <wp n="3">
                  <type type="string">navaid</type>
                  <ident type="string">TALAL</ident>
                  <lon type="double">11.085300</lon>
                  <lat type="double">49.108300</lat>
                </wp>
                <wp n="4">
                  <type type="string">navaid</type>
                  <ident type="string">ERMEL</ident>
                  <lon type="double">11.044700</lon>
                  <lat type="double">49.187800</lat>
                </wp>
                <wp n="5">
                  <type type="string">navaid</type>
                  <ident type="string">PSA</ident>
                  <lon type="double">9.348300</lon>
                  <lat type="double">49.862200</lat>
                </wp>
              </route>
            </PropertyList>
        )";
    }

    auto ourDelegate = TestFPDelegateFactory::delegateForPlan(f);
    CPPUNIT_ASSERT(!ourDelegate->didLoad);

    CPPUNIT_ASSERT(f->load(fgfpPath));

    CPPUNIT_ASSERT(ourDelegate->didLoad);
    CPPUNIT_ASSERT(ourDelegate->sawArrivalChange);
    CPPUNIT_ASSERT(ourDelegate->sawDepartureChange);
}

void FlightplanTests::loadFGFPWithEmbeddedProcedures()
{
    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    FlightPlanRef f = FlightPlan::create();

    SGPath fgfpPath = simgear::Dir::current().path() / "test_fgfp_with_dep_arr.fgfp"s;
    {
        sg_ofstream s(fgfpPath);
        s << R"(<?xml version="1.0" encoding="UTF-8"?>
            <PropertyList>
              <version type="int">2</version>
              <departure>
                <airport type="string">EDDM</airport>
                <runway type="string">08R</runway>
              </departure>
              <destination>
                <airport type="string">EDDF</airport>
              </destination>
              <route>
                    <wp>
                      <type type="string">runway</type>
                      <departure type="bool">true</departure>
                      <ident type="string">08R</ident>
                      <icao type="string">EDDM</icao>
                    </wp>
                <wp n="1">
                  <type type="string">navaid</type>
                  <ident type="string">GIVMI</ident>
                  <lon type="double">11.364700</lon>
                  <lat type="double">48.701100</lat>
                </wp>
                <wp n="2">
                  <type type="string">navaid</type>
                  <ident type="string">ERNAS</ident>
                  <lon type="double">11.219400</lon>
                  <lat type="double">48.844700</lat>
                </wp>
                <wp n="3">
                  <type type="string">navaid</type>
                  <ident type="string">TALAL</ident>
                  <lon type="double">11.085300</lon>
                  <lat type="double">49.108300</lat>
                </wp>
                <wp n="4">
                  <type type="string">navaid</type>
                  <ident type="string">ERMEL</ident>
                  <lon type="double">11.044700</lon>
                  <lat type="double">49.187800</lat>
                </wp>
                <wp n="5">
                  <type type="string">navaid</type>
                  <ident type="string">PSA</ident>
                  <lon type="double">9.348300</lon>
                  <lat type="double">49.862200</lat>
                </wp>

                    <wp>
                      <type type="string">runway</type>
                      <arrival type="bool">true</arrival>
                      <ident type="string">36</ident>
                      <icao type="string">EDDF</icao>
                    </wp>
              </route>
            </PropertyList>
        )";
    }

    auto ourDelegate = TestFPDelegateFactory::delegateForPlan(f);
    CPPUNIT_ASSERT(!ourDelegate->didLoad);

    CPPUNIT_ASSERT(f->load(fgfpPath));

    CPPUNIT_ASSERT(ourDelegate->didLoad);
    CPPUNIT_ASSERT(!ourDelegate->sawArrivalChange);
    CPPUNIT_ASSERT(!ourDelegate->sawDepartureChange);
}

void FlightplanTests::loadFGFPWithOldProcedures()
{
    FlightPlanRef f = FlightPlan::create();

    SGPath fgfpPath = simgear::Dir::current().path() / "test_fgfp_old_procedure_idents.fgfp"s;
    {
        sg_ofstream s(fgfpPath);
        s << R"(<?xml version="1.0" encoding="UTF-8"?>
            <PropertyList>
              <version type="int">2</version>
              <departure>
                <airport type="string">KJFK</airport>
                <runway type="string">13L</runway>
                <sid type="string">CANDR</sid>
              </departure>
              <destination>
                <airport type="string">EHAM</airport>
                <star type="string">BEDUM</star>
              </destination>
              <route>
                    <wp>
                      <type type="string">runway</type>
                      <arrival type="bool">true</arrival>
                      <ident type="string">36</ident>
                      <icao type="string">EDDF</icao>
                    </wp>
              </route>
            </PropertyList>
        )";
    }

    auto kjfk = FGAirport::findByIdent("KJFK"s);
    auto eham = FGAirport::findByIdent("EHAM"s);
    CPPUNIT_ASSERT(f->load(fgfpPath));

    CPPUNIT_ASSERT(f->sid() == nullptr);
    CPPUNIT_ASSERT(f->star() == nullptr);
}

void FlightplanTests::loadFGFPWithProcedureIdents()
{
    FlightPlanRef f = FlightPlan::create();

    SGPath fgfpPath = simgear::Dir::current().path() / "test_fgfp_procedure_idents.fgfp"s;
    {
        sg_ofstream s(fgfpPath);
        s << R"(<?xml version="1.0" encoding="UTF-8"?>
            <PropertyList>
              <version type="int">2</version>
              <departure>
                <airport type="string">KJFK</airport>
                <runway type="string">13L</runway>
                <sid type="string">DEEZZ5.13L</sid>
                <sid_trans type="string">CANDR</sid_trans>
              </departure>
              <destination>
                <airport type="string">EHAM</airport>
                <star type="string">EEL1A</star>
                <star_trans type="string">KUBAT</star_trans>
              </destination>
              <route>
                    <wp>
                      <type type="string">runway</type>
                      <arrival type="bool">true</arrival>
                      <ident type="string">36</ident>
                      <icao type="string">EDDF</icao>
                    </wp>
              </route>
            </PropertyList>
        )";
    }

    auto kjfk = FGAirport::findByIdent("KJFK"s);
    auto eham = FGAirport::findByIdent("EHAM"s);
    CPPUNIT_ASSERT(f->load(fgfpPath));

    CPPUNIT_ASSERT(f->sid());
    CPPUNIT_ASSERT(f->star());

    CPPUNIT_ASSERT_EQUAL(f->sid()->ident(), "DEEZZ5.13L"s);
    CPPUNIT_ASSERT_EQUAL(f->sidTransition()->ident(), "CANDR"s);

    CPPUNIT_ASSERT_EQUAL(f->star()->ident(), "EEL1A"s);
    CPPUNIT_ASSERT_EQUAL(f->starTransition()->ident(), "KUBAT"s);
}

void FlightplanTests::loadFGFPAsRoute()
{
    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    FlightPlanRef f = FlightPlan::createRoute();

    SGPath fgfpPath = simgear::Dir::current().path() / "test_fgfp_as_route.fgfp"s;
    {
        sg_ofstream s(fgfpPath);
        s << R"(<?xml version="1.0" encoding="UTF-8"?>
            <PropertyList>
              <version type="int">2</version>
                <is-route>true</is-route>
              <departure>
                <airport type="string">EGLL</airport>
                <runway type="string">27R</runway>
              </departure>
              <destination>
                <airport type="string">EDDS</airport>
              </destination>
              <route>
                <wp n="1">
                  <type type="string">navaid</type>
                  <ident type="string">BIG</ident>
                        <lat>51.33087500</lat>
                        <lon>000.03481100</lon>
                </wp>
                <wp n="2">
                  <type type="string">navaid</type>
                  <ident type="string">CLN</ident>
                <lat> 51.84841700</lat>
                <lon>001.14761100</lon>
                </wp>
                <wp n="3">
                  <type type="string">via</type>
                  <airway type="string">L620</airway>
                  <level>1</level>
                  <to type="string">BASNO</to>
                    <lat>52.114586</lat>
                    <lon>2.487947</lon>
                </wp>
              </route>
            </PropertyList>
        )";
    }

    auto ourDelegate = TestFPDelegateFactory::delegateForPlan(f);
    CPPUNIT_ASSERT(!ourDelegate->didLoad);

    CPPUNIT_ASSERT(f->load(fgfpPath));
    CPPUNIT_ASSERT(f->isRoute());

    CPPUNIT_ASSERT(ourDelegate->didLoad);
    CPPUNIT_ASSERT(ourDelegate->sawArrivalChange);
    CPPUNIT_ASSERT(ourDelegate->sawDepartureChange);

    CPPUNIT_ASSERT_EQUAL(5, f->numLegs());
    CPPUNIT_ASSERT_EQUAL("via"s, f->legAtIndex(3)->waypoint()->type());

    auto actFP = f->clone("ACT", true);
    CPPUNIT_ASSERT(!actFP->isRoute());
    CPPUNIT_ASSERT(!actFP->isActive());
    // check Via was expanded
    CPPUNIT_ASSERT_EQUAL(8, actFP->numLegs());
}


void FlightplanTests::testCloningBasic()
{
    FlightPlanRef fp1 = makeTestFP("EGCC"s, "23L"s, "EHAM"s, "24"s,
                                   "TNT CLN"s);
    fp1->setIdent("testplan"s);

    fp1->setCruiseAltitudeFt(24000);
    fp1->setCruiseSpeedKnots(448);

    auto fp2 = fp1->clone("testplan2"s);

    CPPUNIT_ASSERT(fp2->ident() == "testplan2"s);
    CPPUNIT_ASSERT(fp2->departureAirport()->ident() == "EGCC"s);
    CPPUNIT_ASSERT(fp2->departureRunway()->ident() == "23L"s);
    CPPUNIT_ASSERT(fp2->destinationAirport()->ident() == "EHAM"s);
    CPPUNIT_ASSERT(fp2->destinationRunway()->ident() == "24"s);
    CPPUNIT_ASSERT_EQUAL(24000, fp2->cruiseAltitudeFt());
    CPPUNIT_ASSERT_EQUAL(448, fp2->cruiseSpeedKnots());

    CPPUNIT_ASSERT_EQUAL(5, fp2->numLegs());

    CPPUNIT_ASSERT(fp2->legAtIndex(0)->waypoint()->source()->ident() == "23L"s);
    CPPUNIT_ASSERT(fp2->legAtIndex(1)->waypoint()->source()->ident() == "TNT"s);
    CPPUNIT_ASSERT(fp2->legAtIndex(2)->waypoint()->source()->ident() == "CLN"s);

}

void FlightplanTests::testCloningFGFP()
{
    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    FlightPlanRef fp1 = FlightPlan::create();

    SGPath fgfpPath = simgear::Dir::current().path() / "test_fgfp_cloning.fgfp"s;
    {
        sg_ofstream s(fgfpPath);
        s << R"(<?xml version="1.0" encoding="UTF-8"?>
            <PropertyList>
              <version type="int">2</version>
              <departure>
                <airport type="string">EDDM</airport>
                <runway type="string">08R</runway>
              </departure>
              <destination>
                <airport type="string">EDDF</airport>
              </destination>
              <route>
                <wp n="1">
                  <type type="string">navaid</type>
                  <ident type="string">GIVMI</ident>
                  <lon type="double">11.364700</lon>
                  <lat type="double">48.701100</lat>
                </wp>
                <wp n="2">
                  <type type="string">navaid</type>
                  <ident type="string">ERNAS</ident>
                  <lon type="double">11.219400</lon>
                  <lat type="double">48.844700</lat>
                </wp>
                <wp n="3">
                  <type type="string">navaid</type>
                  <ident type="string">TALAL</ident>
                  <lon type="double">11.085300</lon>
                  <lat type="double">49.108300</lat>
                </wp>
                <wp n="4">
                  <type type="string">navaid</type>
                  <ident type="string">ERMEL</ident>
                  <lon type="double">11.044700</lon>
                  <lat type="double">49.187800</lat>
                </wp>
                <wp n="5">
                  <type type="string">navaid</type>
                  <ident type="string">PSA</ident>
                  <lon type="double">9.348300</lon>
                  <lat type="double">49.862200</lat>
                </wp>
              </route>
            </PropertyList>
        )";
    }

    auto ourDelegate = TestFPDelegateFactory::delegateForPlan(fp1);
    CPPUNIT_ASSERT(!ourDelegate->didLoad);

    CPPUNIT_ASSERT(fp1->load(fgfpPath));

    CPPUNIT_ASSERT(ourDelegate->didLoad);
    CPPUNIT_ASSERT(ourDelegate->sawArrivalChange);
    CPPUNIT_ASSERT(ourDelegate->sawDepartureChange);

    auto fp2 = fp1->clone();
    auto secondDelegate = TestFPDelegateFactory::delegateForPlan(fp2);

    CPPUNIT_ASSERT(secondDelegate->didLoad);
    CPPUNIT_ASSERT(secondDelegate->sawWaypointsChange);
    CPPUNIT_ASSERT(!secondDelegate->sawArrivalChange);
    CPPUNIT_ASSERT(!secondDelegate->sawDepartureChange);

    CPPUNIT_ASSERT(fp2->departureAirport()->ident() == "EDDM"s);
    CPPUNIT_ASSERT(fp2->departureRunway()->ident() == "08R"s);
    CPPUNIT_ASSERT(fp2->destinationAirport()->ident() == "EDDF"s);

    CPPUNIT_ASSERT_EQUAL(fp2->legAtIndex(0)->waypoint()->source()->ident(), "08R"s);
    CPPUNIT_ASSERT_EQUAL(fp2->legAtIndex(1)->waypoint()->source()->ident(), "GIVMI"s);
    CPPUNIT_ASSERT_EQUAL(fp2->legAtIndex(5)->waypoint()->source()->ident(), "PSA"s);
    CPPUNIT_ASSERT_EQUAL(fp2->legAtIndex(6)->waypoint()->source()->ident(), "EDDF"s);
    CPPUNIT_ASSERT_EQUAL(7, fp2->numLegs());
}

void FlightplanTests::testCloningProcedures()
{
    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    auto egkk = FGAirport::findByIdent("EGKK"s);
    auto sid = egkk->findSIDWithIdent("SAM3P"s);

    FlightPlanRef fp1 = makeTestFP("EGKK"s, "08R"s, "EHAM"s, "18R"s,
                                   ""s);
    //auto ourDelegate = TestFPDelegateFactory::delegateForPlan(fp1);

    fp1->setSID(sid);
    auto eham = FGAirport::findByIdent("EHAM"s);
    auto eel1A = eham->findSTARWithIdent("EEL1A"s);
    CPPUNIT_ASSERT(eel1A);

    fp1->setSTAR(eel1A, "BEDUM"s);

    auto fp2 = fp1->clone();
    CPPUNIT_ASSERT(fp2->departureAirport()->ident() == "EGKK"s);
    CPPUNIT_ASSERT(fp2->departureRunway()->ident() == "08R"s);
    CPPUNIT_ASSERT(fp2->destinationAirport()->ident() == "EHAM"s);
    CPPUNIT_ASSERT(fp2->destinationRunway()->ident() == "18R"s);

    CPPUNIT_ASSERT(fp2->legAtIndex(0)->waypoint()->source()->ident() == "08R"s);
    CPPUNIT_ASSERT_EQUAL(fp2->sid()->ident(), "SAM3P"s);

    CPPUNIT_ASSERT_EQUAL(fp2->star()->ident(), "EEL1A"s);
    CPPUNIT_ASSERT_EQUAL(fp2->starTransition()->ident(), "BEDUM"s);
}


void FlightplanTests::testBug2616()
{
    auto edty = FGAirport::findByIdent("EDTY"s);

    auto ils28Approach = edty->findApproachWithIdent("ILS28"s);
    CPPUNIT_ASSERT(ils28Approach);

        FlightPlanRef fp1 = makeTestFP("EDDS"s, "25"s, "EDTY"s, "28"s,
                                   ""s);



    fp1->setApproach(ils28Approach);

      CPPUNIT_ASSERT(fp1->destinationRunway()->ident() == "28"s);
      CPPUNIT_ASSERT(fp1->approach()->ident() == "ILS28"s);

    fp1->activate();
    CPPUNIT_ASSERT(fp1->isActive());
}

void FlightplanTests::testRoute()
{
  FlightPlanRef fp1 = makeTestFP("EGKK"s, "08R"s, "EHAM"s, "18R"s,
                                   "CLN VIA-L620/REDFA SPY"s, true);

  CPPUNIT_ASSERT(fp1->isRoute());
  CPPUNIT_ASSERT_EQUAL("via"s, fp1->legAtIndex(2)->waypoint()->type());

    // activating a route should fail
    fp1->activate();
    CPPUNIT_ASSERT(!fp1->isActive());

    // setting index should still work
    fp1->setCurrentIndex(2);
    CPPUNIT_ASSERT_EQUAL(2, fp1->currentIndex());
    CPPUNIT_ASSERT(!fp1->isActive());

}

void FlightplanTests::testDeleteProcedureWaypoint()
{
    static_factory = std::make_shared<TestFPDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    // https://gitlab.com/flightgear/flightgear/-/issues/3128

    auto egkk = FGAirport::findByIdent("EGKK"s);
    auto sid = egkk->findSIDWithIdent("SAM3P"s);
    auto eham = FGAirport::findByIdent("EHAM"s);
    auto redf1A = eham->findSTARWithIdent("REDF1A"s);
    auto ils06 = eham->findApproachWithIdent("ILS06"s);
    FlightPlanRef fp1 = makeTestFP("EGKK"s, "08R"s, "EHAM"s, "06"s,
                                   "CLN COA"s, false);
    fp1->setSID(sid);
    fp1->setApproach(ils06, "SUG2A"s);
    fp1->setSTAR(redf1A);

    fp1->activate();

    auto ourDelegate = TestFPDelegateFactory::delegateForPlan(fp1);
    CPPUNIT_ASSERT(ourDelegate->sawArrivalChange);
    CPPUNIT_ASSERT(ourDelegate->sawDepartureChange);

    ourDelegate->sawDepartureChange = false;
    ourDelegate->sawArrivalChange = false;

    // delete KKS02 from the SID
    int sidLegIndex = FGTestApi::flightplan::findLegIndexByIdent(fp1, "KKS02"s);
    CPPUNIT_ASSERT(sidLegIndex >= 0);
    fp1->deleteIndex(sidLegIndex);
    CPPUNIT_ASSERT(!ourDelegate->sawDepartureChange);
    CPPUNIT_ASSERT(!ourDelegate->sawArrivalChange);

    // delete EHA07 from the STAR
    int starLegIndex = FGTestApi::flightplan::findLegIndexByIdent(fp1, "EHA07"s);
    CPPUNIT_ASSERT(starLegIndex >= 0);
    fp1->deleteIndex(starLegIndex);
    CPPUNIT_ASSERT(!ourDelegate->sawDepartureChange);
    CPPUNIT_ASSERT(!ourDelegate->sawArrivalChange);
}
