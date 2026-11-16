// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include <Navaids/FlightPlan.hxx>
#include <simgear/magvar/magvar.hxx>

#include <GUI/ItemModel.hxx>
#include <Navaids/routePath.hxx>

/**
 * @brief Exposes the legs of a FlightPlan as an ItemModel, one row per
 * waypoint.
 *
 * The model observes the FlightPlan via a FlightPlan::Delegate, and emits
 * ItemModel change callbacks as the plan is edited.
 *
 * Supported data keys:
 *  - "ident"     : waypoint ident (std::string)
 *  - "name"      : waypoint name, falling back to the ident if the waypoint
 *                  has no associated navaid (std::string)
 *  - "altitude-ft", "altitude-m", "altitude-fl" : leg altitude in feet,
 *                  metres or flight level (number)
 *  - "altitude-restriction" : "none", "at", "above", "below" or "between"
 *                  (std::string)
 *  - "altitude-units" : units the altitude was specified in: "ft", "m" or
 *                  "fl" (std::string)
 *  - "procedure-ident" : ident of the procedure or airway owning the
 *                  waypoint (std::string, empty if it has none)
 *  - "type"      : waypoint type, eg "basic", "navaid", "runway" (std::string)
 *  - "fly-by"    : false if the waypoint must be overflown (bool)
 *  - "procedure" : "miss", "dep", "arr" or "app" for procedure waypoints
 *                  (std::string, empty otherwise)
 *  - "approach"  : "MAP", "IAF" or "FAF" for those approach waypoints
 *                  (std::string, empty otherwise)
 *  - "geod"      : waypoint position (SGGeod)
 *  - "latitude"  : waypoint latitude, degrees (double)
 *  - "longitude" : waypoint longitude, degrees (double)
 *  - "bearing"   : bearing to the next waypoint, degrees, true or magnetic
 *                  (double)
 *  - "distance"  : distance to the next waypoint, metres (double)
 *  - "label"     : same as "ident"; "value" is the row index
 *
 * "bearing" is a true bearing, unless useMagnetic() is set, in which case
 * it is converted to magnetic using the variation at the waypoint.
 *
 * Hidden waypoints are skipped unless includeHidden() is set. In that case
 * "bearing" and "distance" refer to the next *listed* waypoint: distance
 * includes any hidden legs in between, and bearing is that of the first
 * leg leaving the waypoint.
 *
 * "bearing" and "distance" are derived from a cached RoutePath, so they
 * follow the computed path (procedures, holds, vias) rather than straight
 * lines between waypoints. They are empty for the final waypoint.
 */
class FlightPlanModel : public ItemModel
{
public:
    FlightPlanModel();
    explicit FlightPlanModel(flightgear::FlightPlanRef fp);
    ~FlightPlanModel() override;

    flightgear::FlightPlanRef flightPlan() const { return _flightPlan; }

    /**
     * @brief Change the flight plan being displayed. May be null, in which
     * case the model is empty.
     */
    void setFlightPlan(flightgear::FlightPlanRef fp);

    /**
     * @brief Whether bearings are reported in magnetic or true degrees.
     */
    bool useMagnetic() const { return _useMagnetic; }
    void setUseMagnetic(bool magnetic);

    /**
     * @brief Whether waypoints flagged WPT_HIDDEN (eg procedure
     * housekeeping points) are listed. Defaults to false.
     */
    bool includeHidden() const { return _includeHidden; }
    void setIncludeHidden(bool include);

    size_t count() const override;
    std::any dataAt(size_t index, const std::string& key) const override;

private:
    class Delegate;
    friend class Delegate;

    void flightPlanChanged();
    void rebuildRows();
    const RoutePath* routePath() const;

    flightgear::FlightPlanRef _flightPlan;
    std::unique_ptr<Delegate> _delegate;
    bool _useMagnetic = true;
    mutable SGMagVar _magVar;
    bool _includeHidden = false;
    std::vector<int> _rows;                        ///< leg index for each model row
    mutable std::unique_ptr<RoutePath> _routePath; ///< built lazily, dropped on any change
};
