// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#include "config.h"

#include "FlightPlanModel.hxx"

#include <simgear/timing/sg_time.hxx>

#include <Main/globals.hxx>
#include <Navaids/positioned.hxx>
#include <Navaids/route.hxx>

class FlightPlanModel::Delegate : public flightgear::FlightPlan::Delegate
{
public:
    explicit Delegate(FlightPlanModel* m)
        : _model(m)
    {
    }

    void waypointsChanged() override { _model->flightPlanChanged(); }
    void cleared() override { _model->flightPlanChanged(); }
    void loaded() override { _model->flightPlanChanged(); }

    // departure / arrival changes can insert or remove procedure waypoints,
    // but those also trigger waypointsChanged
    void arrivalChanged() override { _model->flightPlanChanged(); }
    void departureChanged() override { _model->flightPlanChanged(); }

private:
    FlightPlanModel* _model;
};

FlightPlanModel::FlightPlanModel() = default;

FlightPlanModel::FlightPlanModel(flightgear::FlightPlanRef fp)
{
    setFlightPlan(fp);
}

FlightPlanModel::~FlightPlanModel()
{
    if (_flightPlan && _delegate) {
        _flightPlan->removeDelegate(_delegate.get());
    }
}

void FlightPlanModel::setFlightPlan(flightgear::FlightPlanRef fp)
{
    if (_flightPlan == fp) {
        return;
    }

    if (_flightPlan && _delegate) {
        _flightPlan->removeDelegate(_delegate.get());
    }
    _delegate.reset();

    _routePath.reset();
    _flightPlan = fp;
    if (_flightPlan) {
        _delegate.reset(new Delegate(this));
        _flightPlan->addDelegate(_delegate.get());
    }

    rebuildRows();
    didReset();
}

void FlightPlanModel::setUseMagnetic(bool magnetic)
{
    if (_useMagnetic == magnetic) {
        return;
    }

    _useMagnetic = magnetic;
    if (count() > 0) {
        didChangeData(0, static_cast<int>(count()));
    }
}

void FlightPlanModel::setIncludeHidden(bool include)
{
    if (_includeHidden == include) {
        return;
    }

    _includeHidden = include;
    rebuildRows();
    didReset();
}

void FlightPlanModel::rebuildRows()
{
    _rows.clear();
    if (!_flightPlan) {
        return;
    }

    for (int i = 0; i < _flightPlan->numLegs(); ++i) {
        if (_includeHidden || !_flightPlan->legAtIndex(i)->waypoint()->flag(flightgear::WPT_HIDDEN)) {
            _rows.push_back(i);
        }
    }
}

void FlightPlanModel::flightPlanChanged()
{
    _routePath.reset();
    const auto oldCount = _rows.size();
    rebuildRows();
    const auto newCount = _rows.size();
    if (newCount == oldCount) {
        // we can't tell which legs changed, so report them all as modified
        // (this preserves view state such as selection)
        if (newCount > 0) {
            didChangeData(0, static_cast<int>(newCount));
        }
        return;
    }

    didReset();
}

const RoutePath* FlightPlanModel::routePath() const
{
    if (!_routePath && _flightPlan) {
        _routePath.reset(new RoutePath(_flightPlan.get()));
    }
    return _routePath.get();
}

size_t FlightPlanModel::count() const
{
    return _rows.size();
}

std::any FlightPlanModel::dataAt(size_t index, const std::string& key) const
{
    if (!_flightPlan || index >= count()) {
        return {};
    }

    const int legIndex = _rows.at(index);
    const auto leg = _flightPlan->legAtIndex(legIndex);
    if (!leg) {
        return {};
    }

    const auto wpt = leg->waypoint();
    if (key == "ident" || key == "label") {
        return wpt->ident();
    } else if (key == "value") {
        return static_cast<int>(index);
    } else if (key == "name") {
        if (const auto pos = wpt->source()) {
            return pos->name();
        }
        return wpt->ident();
    } else if (key == "altitude-restriction") {
        switch (leg->altitudeRestriction()) {
        case flightgear::RESTRICT_AT: return std::string("at");
        case flightgear::RESTRICT_ABOVE: return std::string("above");
        case flightgear::RESTRICT_BELOW: return std::string("below");
        case flightgear::RESTRICT_BETWEEN: return std::string("between");
        default: return std::string("none");
        }
    } else if (key == "altitude-units") {
        switch (leg->altitudeUnits()) {
        case flightgear::ALTITUDE_METER: return std::string("m");
        case flightgear::ALTITUDE_FLIGHTLEVEL: return std::string("fl");
        default: return std::string("ft");
        }
    } else if (key == "altitude-ft") {
        return leg->altitudeFt();
    } else if (key == "altitude-m") {
        return leg->altitude(flightgear::ALTITUDE_METER);
    } else if (key == "altitude-fl") {
        return leg->altitude(flightgear::ALTITUDE_FLIGHTLEVEL);
    } else if (key == "procedure-ident") {
        // the procedure (SID, STAR, approach) or airway the waypoint belongs to
        if (const auto owner = wpt->owner()) {
            return owner->ident();
        }
        return {};
    } else if (key == "type") {
        return wpt->type();
    } else if (key == "fly-by") {
        return !wpt->flag(flightgear::WPT_OVERFLIGHT);
    } else if (key == "procedure") {
        // a missed approach waypoint is typically also flagged as approach,
        // so test it first
        if (wpt->flag(flightgear::WPT_MISS)) return std::string("miss");
        if (wpt->flag(flightgear::WPT_DEPARTURE)) return std::string("dep");
        if (wpt->flag(flightgear::WPT_ARRIVAL)) return std::string("arr");
        if (wpt->flag(flightgear::WPT_APPROACH)) return std::string("app");
        return {};
    } else if (key == "approach") {
        if (wpt->flag(flightgear::WPT_MAP)) return std::string("MAP");
        if (wpt->flag(flightgear::WPT_IAF)) return std::string("IAF");
        if (wpt->flag(flightgear::WPT_FAF)) return std::string("FAF");
        return {};
    } else if (key == "geod") {
        // FIXME: for this to be usable from Nasal, we need to make
        // any_to_nasal extensible on the FG side, so we can build a Geod
        // instance.
        return wpt->position();
    } else if (key == "latitude") {
        return wpt->position().getLatitudeDeg();
    } else if (key == "longitude") {
        return wpt->position().getLongitudeDeg();
    } else if (key == "bearing" || key == "distance") {
        if (index + 1 >= count()) {
            return {}; // last waypoint has no next
        }

        // RoutePath describes the leg arriving at an index, so the leg from
        // this waypoint to the next is the one for legIndex + 1. The next
        // listed waypoint may be further on if hidden ones are skipped.
        const auto path = routePath();
        if (key == "distance") {
            return path->distanceBetweenIndices(legIndex, _rows.at(index + 1));
        }

        double track = path->trackForIndex(legIndex + 1);
        if (_useMagnetic) {
            _magVar.update(wpt->position(), globals->get_time_params()->getJD());
            // variation is positive east, magnetic = true - variation
            track -= _magVar.get_magvar() * SG_RADIANS_TO_DEGREES;
            track = SGMiscd::normalizePeriodic(0.0, 360.0, track);
        }
        return track;
    }

    return {};
}
