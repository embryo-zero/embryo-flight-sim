/*
 * SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_flightPlanModel.hxx"
#include "config.h"

#include <cmath>
#include <memory>
#include <vector>

#include "test_suite/FGTestApi/NavDataCache.hxx"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <simgear/magvar/magvar.hxx>
#include <simgear/math/SGGeodesy.hxx>
#include <simgear/structure/exception.hxx>
#include <simgear/timing/sg_time.hxx>

#include <Airports/airport.hxx>
#include <GUI/FlightPlanModel.hxx>
#include <Main/globals.hxx>
#include <Navaids/FlightPlan.hxx>
#include <Navaids/routePath.hxx>
#include <Navaids/waypoint.hxx>

using namespace std::string_literals;
using namespace flightgear;

namespace {

// Mimics the default route-manager delegate, so that setting a SID / STAR /
// approach on the plan inserts the procedure waypoints.
class ProcedureDelegate : public FlightPlan::Delegate
{
public:
    explicit ProcedureDelegate(FlightPlan* plan)
        : _plan(plan)
    {
    }

    void departureChanged() override
    {
        _plan->clearWayptsWithFlag(WPT_DEPARTURE);

        auto dr = new RunwayWaypt(_plan->departureRunway(), _plan);
        dr->setFlag(WPT_DEPARTURE);
        dr->setFlag(WPT_GENERATED);
        _plan->insertWayptAtIndex(dr, 0);

        if (_plan->sid()) {
            WayptVec sidRoute;
            if (!_plan->sid()->route(_plan->departureRunway(), _plan->sidTransition(), sidRoute)) {
                throw sg_exception("failed to route via SID");
            }
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
        _plan->clearWayptsWithFlag(WPT_ARRIVAL);
        _plan->clearWayptsWithFlag(WPT_APPROACH);

        if (!_plan->destinationAirport() || !_plan->destinationRunway()) {
            return;
        }

        auto dr = new RunwayWaypt(_plan->destinationRunway(), _plan);
        dr->setFlag(WPT_APPROACH);
        dr->setFlag(WPT_GENERATED);
        auto leg = _plan->insertWayptAtIndex(dr, -1);

        if (_plan->star()) {
            WayptVec starRoute;
            if (!_plan->star()->route(_plan->destinationRunway(), _plan->starTransition(), starRoute)) {
                throw sg_exception("failed to route via STAR");
            }
            int insertIndex = leg->index();
            for (auto w : starRoute) {
                w->setFlag(WPT_ARRIVAL);
                w->setFlag(WPT_GENERATED);
                _plan->insertWayptAtIndex(w, insertIndex++);
            }
        }

        if (_plan->approach()) {
            WayptVec approachRoute;
            if (!_plan->approach()->routeFromVectors(approachRoute)) {
                throw sg_exception("failed to route approach");
            }
            int insertIndex = leg->index();
            for (auto w : approachRoute) {
                w->setFlag(WPT_APPROACH);
                w->setFlag(WPT_GENERATED);
                _plan->insertWayptAtIndex(w, insertIndex++);
            }
        }
    }

private:
    FlightPlan* _plan;
};

class ProcedureDelegateFactory : public FlightPlan::DelegateFactory
{
public:
    FlightPlan::Delegate* createFlightPlanDelegate(FlightPlan* fp) override
    {
        return new ProcedureDelegate(fp);
    }

    void destroyFlightPlanDelegate(FlightPlan*, FlightPlan::Delegate* d) override
    {
        delete d;
    }
};

std::shared_ptr<ProcedureDelegateFactory> static_factory;

// records the change callbacks fired by a model
struct Observer {
    struct Event {
        ItemModel::Change change;
        size_t row, count;
    };

    explicit Observer(ItemModel* m)
    {
        m->addChangeCallback([this](ItemModel::Change c, size_t r, size_t n) {
            events.push_back({c, r, n});
        });
    }

    bool sawChange(ItemModel::Change c) const
    {
        for (const auto& e : events) {
            if (e.change == c) return true;
        }
        return false;
    }

    std::vector<Event> events;
};

FlightPlanRef makeFP(const std::string& depICAO, const std::string& depRunway,
                     const std::string& destICAO, const std::string& destRunway,
                     const std::string& waypoints)
{
    FlightPlanRef f = FlightPlan::create();
    FGTestApi::setUp::populateFPWithoutNasal(f, depICAO, depRunway, destICAO, destRunway, waypoints);
    return f;
}

// plan with a SID, a STAR and an approach, for the procedure tests
FlightPlanRef makeProcedureFP()
{
    // must be registered before the plan is created, but only for tests which
    // want procedures: it also inserts the runway waypoints itself
    static_factory = std::make_shared<ProcedureDelegateFactory>();
    FlightPlan::registerDelegateFactory(static_factory);

    auto egkk = FGAirport::findByIdent("EGKK"s);
    auto eham = FGAirport::findByIdent("EHAM"s);

    FlightPlanRef fp = makeFP("EGKK"s, "08R"s, "EHAM"s, "06"s, "CLN COA"s);
    fp->setSID(egkk->findSIDWithIdent("SAM3P"s));
    fp->setApproach(eham->findApproachWithIdent("ILS06"s), "SUG2A"s);
    fp->setSTAR(eham->findSTARWithIdent("REDF1A"s));
    return fp;
}

int rowForIdent(const ItemModel& m, const std::string& ident)
{
    for (size_t i = 0; i < m.count(); ++i) {
        if (std::any_cast<std::string>(m.dataAt(i, "ident")) == ident) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string stringAt(const ItemModel& m, size_t row, const std::string& key)
{
    return std::any_cast<std::string>(m.dataAt(row, key));
}

} // namespace

void FlightPlanModelTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("flightplan"s);
    FGTestApi::setUp::initNavDataCache();

    globals->get_subsystem_mgr()->bind();
    globals->get_subsystem_mgr()->init();
    globals->get_subsystem_mgr()->postinit();
}

void FlightPlanModelTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();

    if (static_factory) {
        FlightPlan::unregisterDelegateFactory(static_factory);
        static_factory.reset();
    }
}

void FlightPlanModelTests::testBasicData()
{
    FlightPlanRef fp = makeFP("EGCC"s, "23L"s, "EHAM"s, "24"s, "TNT CLN"s);
    FlightPlanModel model(fp);
    model.setUseMagnetic(false);

    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());

    CPPUNIT_ASSERT_EQUAL("EGCC-23L"s, stringAt(model, 0, "ident"));
    CPPUNIT_ASSERT_EQUAL("runway"s, stringAt(model, 0, "type"));
    CPPUNIT_ASSERT_EQUAL("TNT"s, stringAt(model, 1, "ident"));
    CPPUNIT_ASSERT_EQUAL("TNT"s, stringAt(model, 1, "label"));
    CPPUNIT_ASSERT_EQUAL("TRENT VOR-DME"s, stringAt(model, 1, "name"));
    CPPUNIT_ASSERT_EQUAL("navaid"s, stringAt(model, 1, "type"));
    CPPUNIT_ASSERT_EQUAL("CLN"s, stringAt(model, 2, "ident"));
    CPPUNIT_ASSERT_EQUAL("EHAM-24"s, stringAt(model, 4, "ident"));

    // out of range rows / unknown keys are empty
    CPPUNIT_ASSERT(!model.dataAt(99, "ident").has_value());
    CPPUNIT_ASSERT(!model.dataAt(1, "no-such-key").has_value());

    // position
    const auto tnt = fp->legAtIndex(1)->waypoint()->position();
    const auto geod = std::any_cast<SGGeod>(model.dataAt(1, "geod"));
    CPPUNIT_ASSERT_DOUBLES_EQUAL(tnt.getLatitudeDeg(), geod.getLatitudeDeg(), 1e-6);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(tnt.getLongitudeDeg(), geod.getLongitudeDeg(), 1e-6);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(tnt.getLatitudeDeg(), std::any_cast<double>(model.dataAt(1, "latitude")), 1e-6);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(tnt.getLongitudeDeg(), std::any_cast<double>(model.dataAt(1, "longitude")), 1e-6);

    // bearing & distance come from the RoutePath, for the leg to the next waypoint
    RoutePath path(fp);
    for (size_t i = 0; i + 1 < model.count(); ++i) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(path.trackForIndex(i + 1),
                                     std::any_cast<double>(model.dataAt(i, "bearing")), 0.01);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(path.distanceForIndex(i + 1),
                                     std::any_cast<double>(model.dataAt(i, "distance")), 0.5);
    }

    // sanity check against a straight line TNT -> CLN (no procedures involved)
    const auto cln = fp->legAtIndex(2)->waypoint()->position();
    CPPUNIT_ASSERT_DOUBLES_EQUAL(SGGeodesy::distanceM(tnt, cln),
                                 std::any_cast<double>(model.dataAt(1, "distance")), 2000.0);

    // no next waypoint for the final row
    CPPUNIT_ASSERT(!model.dataAt(4, "bearing").has_value());
    CPPUNIT_ASSERT(!model.dataAt(4, "distance").has_value());

    // plain waypoints: fly-by, and not part of any procedure
    CPPUNIT_ASSERT(std::any_cast<bool>(model.dataAt(1, "fly-by")));
    CPPUNIT_ASSERT(!model.dataAt(1, "procedure").has_value());
    CPPUNIT_ASSERT(!model.dataAt(1, "approach").has_value());

    fp->legAtIndex(1)->waypoint()->setFlag(WPT_OVERFLIGHT);
    CPPUNIT_ASSERT(!std::any_cast<bool>(model.dataAt(1, "fly-by")));
}

void FlightPlanModelTests::testProcedures()
{
    FlightPlanRef fp = makeProcedureFP();
    FlightPlanModel model(fp);

    CPPUNIT_ASSERT_EQUAL((size_t)fp->numLegs(), model.count());

    // SID waypoints are 'dep', STAR waypoints are 'arr', and the enroute
    // waypoints entered by the user aren't part of any procedure
    const int sidRow = rowForIdent(model, "KKS02"s);
    CPPUNIT_ASSERT(sidRow > 0);
    CPPUNIT_ASSERT_EQUAL("dep"s, stringAt(model, sidRow, "procedure"));
    CPPUNIT_ASSERT_EQUAL("dep"s, stringAt(model, 0, "procedure")); // departure runway

    const int starRow = rowForIdent(model, "EHA07"s);
    CPPUNIT_ASSERT(starRow > sidRow);
    CPPUNIT_ASSERT_EQUAL("arr"s, stringAt(model, starRow, "procedure"));

    const int clnRow = rowForIdent(model, "CLN"s);
    CPPUNIT_ASSERT(clnRow > sidRow);
    CPPUNIT_ASSERT(!model.dataAt(clnRow, "procedure").has_value());

    // the final row is the destination runway, flagged as approach
    const size_t lastRow = model.count() - 1;
    CPPUNIT_ASSERT_EQUAL("EHAM-06"s, stringAt(model, lastRow, "ident"));
    CPPUNIT_ASSERT_EQUAL("app"s, stringAt(model, lastRow, "procedure"));

    // 'approach' values, where present, are one of the known strings; the
    // flags must agree with the underlying waypoint
    for (size_t i = 0; i < model.count(); ++i) {
        const auto wpt = fp->legAtIndex(i)->waypoint();
        const auto a = model.dataAt(i, "approach");
        if (wpt->flag(WPT_MAP)) {
            CPPUNIT_ASSERT_EQUAL("MAP"s, std::any_cast<std::string>(a));
        } else if (wpt->flag(WPT_IAF)) {
            CPPUNIT_ASSERT_EQUAL("IAF"s, std::any_cast<std::string>(a));
        } else if (wpt->flag(WPT_FAF)) {
            CPPUNIT_ASSERT_EQUAL("FAF"s, std::any_cast<std::string>(a));
        } else {
            CPPUNIT_ASSERT(!a.has_value());
        }
    }

    // missed approach flag takes priority over approach
    fp->legAtIndex(clnRow)->waypoint()->setFlag(WPT_MISS);
    fp->legAtIndex(clnRow)->waypoint()->setFlag(WPT_APPROACH);
    CPPUNIT_ASSERT_EQUAL("miss"s, stringAt(model, clnRow, "procedure"));

    fp->legAtIndex(clnRow)->waypoint()->setFlag(WPT_FAF);
    CPPUNIT_ASSERT_EQUAL("FAF"s, stringAt(model, clnRow, "approach"));
}

void FlightPlanModelTests::testEdits()
{
    FlightPlanRef fp = makeFP("EGCC"s, "23L"s, "EHAM"s, "24"s, "TNT CLN"s);
    FlightPlanModel model(fp);
    Observer obs(&model);

    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());

    // delete CLN
    fp->deleteIndex(2);
    CPPUNIT_ASSERT_EQUAL((size_t)4, model.count());
    CPPUNIT_ASSERT_EQUAL(-1, rowForIdent(model, "CLN"s));
    CPPUNIT_ASSERT_EQUAL("EHAM-24"s, stringAt(model, 3, "ident"));
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));

    // the bearing / distance of the (new) final row must have gone away, and
    // those of the row before it updated: they now point at the destination
    CPPUNIT_ASSERT(!model.dataAt(3, "distance").has_value());
    const auto tnt = fp->legAtIndex(1)->waypoint()->position();
    const auto rwy = fp->legAtIndex(3)->waypoint()->position();
    CPPUNIT_ASSERT_DOUBLES_EQUAL(SGGeodesy::distanceM(tnt, rwy),
                                 std::any_cast<double>(model.dataAt(1, "distance")),
                                 SGGeodesy::distanceM(tnt, rwy) * 0.05);

    // insert a waypoint
    obs.events.clear();
    auto wp = new BasicWaypt(SGGeod::fromDeg(0.5, 52.0), "NEWWP"s, fp);
    fp->insertWayptAtIndex(wp, 2);
    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());
    CPPUNIT_ASSERT_EQUAL("NEWWP"s, stringAt(model, 2, "ident"));
    CPPUNIT_ASSERT_EQUAL("basic"s, stringAt(model, 2, "type"));
    CPPUNIT_ASSERT_EQUAL("EHAM-24"s, stringAt(model, 4, "ident"));
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));

    // modify a waypoint in place: same count, so rows are reported modified
    obs.events.clear();
    fp->legAtIndex(2)->setAltitude(RESTRICT_AT, 5000.0, ALTITUDE_FEET);
    fp->legAtIndex(2)->markWaypointDirty();
    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Modified));
    CPPUNIT_ASSERT(!obs.sawChange(ItemModel::Change::Reset));

    // clear everything
    obs.events.clear();
    fp->clearAll();
    CPPUNIT_ASSERT_EQUAL((size_t)0, model.count());
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));
}

void FlightPlanModelTests::testProcedureEdits()
{
    FlightPlanRef fp = makeProcedureFP();
    FlightPlanModel model(fp);
    Observer obs(&model);

    const size_t initialCount = model.count();
    const int sidRow = rowForIdent(model, "KKS02"s);
    CPPUNIT_ASSERT(sidRow >= 0);

    // delete a SID waypoint
    fp->deleteIndex(sidRow);
    CPPUNIT_ASSERT_EQUAL(initialCount - 1, model.count());
    CPPUNIT_ASSERT_EQUAL(-1, rowForIdent(model, "KKS02"s));
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));

    // delete a STAR waypoint
    const int starRow = rowForIdent(model, "EHA07"s);
    CPPUNIT_ASSERT(starRow >= 0);
    fp->deleteIndex(starRow);
    CPPUNIT_ASSERT_EQUAL(initialCount - 2, model.count());
    CPPUNIT_ASSERT_EQUAL(-1, rowForIdent(model, "EHA07"s));

    // the model must still agree with the plan, and the flags still apply
    CPPUNIT_ASSERT_EQUAL((size_t)fp->numLegs(), model.count());
    CPPUNIT_ASSERT_EQUAL("dep"s, stringAt(model, 0, "procedure"));

    // changing the SID rebuilds the departure waypoints
    obs.events.clear();
    auto egkk = FGAirport::findByIdent("EGKK"s);
    fp->setSID(static_cast<SID*>(nullptr));
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));
    CPPUNIT_ASSERT_EQUAL((size_t)fp->numLegs(), model.count());
    CPPUNIT_ASSERT_EQUAL(-1, rowForIdent(model, "KKS02"s));
    CPPUNIT_ASSERT_EQUAL("EGKK-08R"s, stringAt(model, 0, "ident"));

    fp->setSID(egkk->findSIDWithIdent("SAM3P"s));
    CPPUNIT_ASSERT_EQUAL((size_t)fp->numLegs(), model.count());
    CPPUNIT_ASSERT(rowForIdent(model, "KKS02"s) > 0);

    // removing the STAR removes all the 'arr' rows
    fp->setSTAR(static_cast<STAR*>(nullptr));
    CPPUNIT_ASSERT_EQUAL((size_t)fp->numLegs(), model.count());
    for (size_t i = 0; i < model.count(); ++i) {
        const auto p = model.dataAt(i, "procedure");
        CPPUNIT_ASSERT(!p.has_value() || std::any_cast<std::string>(p) != "arr"s);
    }
}

void FlightPlanModelTests::testHidden()
{
    FlightPlanRef fp = makeFP("EGCC"s, "23L"s, "EHAM"s, "24"s, "TNT CLN"s);
    FlightPlanModel model(fp);
    model.setUseMagnetic(false);

    CPPUNIT_ASSERT(!model.includeHidden());
    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());

    // hide CLN
    fp->legAtIndex(2)->waypoint()->setFlag(WPT_HIDDEN);
    fp->legAtIndex(2)->markWaypointDirty();

    CPPUNIT_ASSERT_EQUAL((size_t)4, model.count());
    CPPUNIT_ASSERT_EQUAL(-1, rowForIdent(model, "CLN"s));
    CPPUNIT_ASSERT_EQUAL("TNT"s, stringAt(model, 1, "ident"));
    CPPUNIT_ASSERT_EQUAL("EHAM-24"s, stringAt(model, 3, "ident"));

    // the legs either side of the hidden waypoint are combined: distance
    // from TNT is to the *next listed* waypoint, via the hidden one
    RoutePath path(fp);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(path.distanceBetweenIndices(1, 3),
                                 std::any_cast<double>(model.dataAt(1, "distance")), 0.5);
    // ...and the bearing is that of the first leg leaving TNT
    CPPUNIT_ASSERT_DOUBLES_EQUAL(path.trackForIndex(2),
                                 std::any_cast<double>(model.dataAt(1, "bearing")), 0.01);

    // show hidden
    Observer obs(&model);
    model.setIncludeHidden(true);
    CPPUNIT_ASSERT(model.includeHidden());
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));
    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());
    CPPUNIT_ASSERT_EQUAL("CLN"s, stringAt(model, 2, "ident"));

    // and hide again
    model.setIncludeHidden(false);
    CPPUNIT_ASSERT_EQUAL((size_t)4, model.count());

    // un-hiding in the plan restores the row
    fp->legAtIndex(2)->waypoint()->setFlag(WPT_HIDDEN, false);
    fp->legAtIndex(2)->markWaypointDirty();
    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());
    CPPUNIT_ASSERT_EQUAL("CLN"s, stringAt(model, 2, "ident"));
}

void FlightPlanModelTests::testMagnetic()
{
    FlightPlanRef fp = makeFP("EGCC"s, "23L"s, "EHAM"s, "24"s, "TNT CLN"s);
    FlightPlanModel model(fp);

    model.setUseMagnetic(false);
    CPPUNIT_ASSERT(!model.useMagnetic());
    const double trueBrg = std::any_cast<double>(model.dataAt(1, "bearing"));

    Observer obs(&model);
    model.setUseMagnetic(true);
    CPPUNIT_ASSERT(model.useMagnetic());
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Modified));
    const double magBrg = std::any_cast<double>(model.dataAt(1, "bearing"));

    // magnetic = true - variation, at the start waypoint
    const double jd = globals->get_time_params()->getJD();
    const auto pos = fp->legAtIndex(1)->waypoint()->position();
    const double magvarDeg = sgGetMagVar(pos, jd) * SG_RADIANS_TO_DEGREES;
    CPPUNIT_ASSERT(std::fabs(magvarDeg) > 0.1); // otherwise this test proves nothing

    double expected = std::fmod(trueBrg - magvarDeg + 360.0, 360.0);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, magBrg, 0.01);

    // distance isn't affected
    const double dist = std::any_cast<double>(model.dataAt(1, "distance"));
    model.setUseMagnetic(false);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(dist, std::any_cast<double>(model.dataAt(1, "distance")), 0.001);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(trueBrg, std::any_cast<double>(model.dataAt(1, "bearing")), 0.001);
}

void FlightPlanModelTests::testChangeFlightPlan()
{
    FlightPlanModel model;
    Observer obs(&model);
    CPPUNIT_ASSERT_EQUAL((size_t)0, model.count());
    CPPUNIT_ASSERT(!model.dataAt(0, "ident").has_value());

    FlightPlanRef fp1 = makeFP("EGCC"s, "23L"s, "EHAM"s, "24"s, "TNT CLN"s);
    model.setFlightPlan(fp1);
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));
    CPPUNIT_ASSERT_EQUAL((size_t)5, model.count());

    FlightPlanRef fp2 = makeFP("EGKK"s, "08R"s, "EHAM"s, "06"s, "CLN"s);
    obs.events.clear();
    model.setFlightPlan(fp2);
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));
    CPPUNIT_ASSERT_EQUAL((size_t)fp2->numLegs(), model.count());
    CPPUNIT_ASSERT_EQUAL("EGKK-08R"s, stringAt(model, 0, "ident"));

    // edits to the old plan must no longer reach the model
    obs.events.clear();
    fp1->deleteIndex(1);
    CPPUNIT_ASSERT(obs.events.empty());
    CPPUNIT_ASSERT_EQUAL((size_t)fp2->numLegs(), model.count());

    // ...but edits to the new one do
    fp2->deleteIndex(1);
    CPPUNIT_ASSERT(!obs.events.empty());
    CPPUNIT_ASSERT_EQUAL((size_t)fp2->numLegs(), model.count());

    // detach
    obs.events.clear();
    model.setFlightPlan(nullptr);
    CPPUNIT_ASSERT(obs.sawChange(ItemModel::Change::Reset));
    CPPUNIT_ASSERT_EQUAL((size_t)0, model.count());
    CPPUNIT_ASSERT(!model.flightPlan());
}
