// SPDX-FileCopyrightText: 2026 Stuart Buchanan
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <Navaids/FlightPlan.hxx>
#include <Navaids/fix.hxx>
#include <Navaids/route.hxx>
#include <Navaids/waypoint.hxx>

using namespace flightgear;

namespace FGTestApi {
namespace flightplan {

static WayptRef hdgToAlt(FlightPlan* f, const std::string& ident, double hdg, double altFt)
{
    auto w = new HeadingToAltitude(f, ident, hdg);
    w->setAltitude(altFt, RESTRICT_ABOVE);
    return w;
}

static WayptRef fix(FlightPlan* f, const std::string& ident, double lat, double lon)
{
    return new BasicWaypt(SGGeod::fromDeg(lon, lat), ident, f);
}

} // namespace flightplan
} // namespace FGTestApi
