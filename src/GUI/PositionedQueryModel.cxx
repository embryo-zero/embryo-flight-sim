// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#include "config.h"

#include "PositionedQueryModel.hxx"

#include <algorithm>
#include <set>

#include <simgear/constants.h>
#include <simgear/debug/logstream.hxx>
#include <simgear/math/sg_geodesy.hxx>
#include <simgear/misc/strutils.hxx>

#include <Main/globals.hxx>
#include <Navaids/NavDataCache.hxx>

using namespace std::string_literals;

namespace {

constexpr size_t MIN_SEARCH_LENGTH = 3;
constexpr size_t MAX_RESULTS = 200;

} // namespace

PositionedQueryModel::PositionedQueryModel() = default;

void PositionedQueryModel::setSearchTerm(const std::string& search)
{
    if (search == _searchTerm) {
        return;
    }

    _searchTerm = search;
    requery();
}

void PositionedQueryModel::setQueryNavaids(bool b)
{
    if (b == _queryVOR && b == _queryNDB) {
        return;
    }

    _queryVOR = _queryNDB = b;
    requery();
}

void PositionedQueryModel::setQueryVOR(bool b)
{
    if (b == _queryVOR) {
        return;
    }

    _queryVOR = b;
    requery();
}

void PositionedQueryModel::setQueryNDB(bool b)
{
    if (b == _queryNDB) {
        return;
    }

    _queryNDB = b;
    requery();
}

void PositionedQueryModel::setQueryWaypoints(bool b)
{
    if (b == _queryWaypoints) {
        return;
    }

    _queryWaypoints = b;
    requery();
}

void PositionedQueryModel::setPosition(const SGGeod& pos)
{
    _position = pos;
    requery();
}

void PositionedQueryModel::setPositionLatLon(double latDeg, double lonDeg)
{
    setPosition(SGGeod::fromDeg(lonDeg, latDeg));
}

void PositionedQueryModel::clearPosition()
{
    _position.reset();
    requery();
}

SGGeod PositionedQueryModel::referencePosition() const
{
    return _position ? *_position : globals->get_aircraft_position();
}

void PositionedQueryModel::requery()
{
    _data.clear();

    if (_searchTerm.size() < MIN_SEARCH_LENGTH) {
        didReset();
        return;
    }

    FGPositioned::TypeFilter filter;
    if (_queryVOR) {
        filter.addType(FGPositioned::VOR);
    }
    if (_queryNDB) {
        filter.addType(FGPositioned::NDB);
    }
    if (_queryWaypoints) {
        filter.addType(FGPositioned::FIX);
    }

    if (!_queryVOR && !_queryNDB && !_queryWaypoints) {
        didReset();
        return;
    }

    auto cache = flightgear::NavDataCache::instance();

    // idents are always upper-case; the name match is case-insensitive in SQLite
    FGPositionedList matches = cache->findAllWithIdent(simgear::strutils::uppercase(_searchTerm), &filter, false);
    FGPositionedList byName = cache->findAllWithName(_searchTerm, &filter, false);
    matches.insert(matches.end(), byName.begin(), byName.end());

    // remove duplicates (ident and name both matching)
    std::set<PositionedID> seen;
    matches.erase(std::remove_if(matches.begin(), matches.end(),
                                 [&seen](const FGPositionedRef& p) {
                                     return !seen.insert(p->guid()).second;
                                 }),
                  matches.end());

    const SGGeod pos = referencePosition();
    FGPositioned::sortByRange(matches, pos);
    const SGVec3d cartPos = SGVec3d::fromGeod(pos); // same metric as sortByRange
    if (matches.size() > MAX_RESULTS) {
        matches.resize(MAX_RESULTS);
    }

    _data.reserve(matches.size());
    for (const auto& p : matches) {
        Entry e;
        e.id = p->guid();
        e.ident = p->ident();
        e.type = p->type();
        // waypoints have no meaningful name distinct from their ident
        if (e.type != FGPositioned::FIX) {
            e.name = p->name();
        }
        e.distanceNm = length(p->cart() - cartPos) * SG_METER_TO_NM;
        _data.push_back(std::move(e));
    }

    didReset();
}

size_t PositionedQueryModel::count() const
{
    return _data.size();
}

std::any PositionedQueryModel::dataAt(size_t index, const std::string& key) const
{
    if (index >= _data.size()) {
        return {};
    }

    const auto& d = _data.at(index);
    if (key == "label") {
        if (d.name.empty()) {
            return d.ident;
        }
        return d.ident + " ("s + d.name + ")"s;
    } else if (key == "value") {
        return d.id;
    } else if (key == "ident") {
        return d.ident;
    } else if (key == "name") {
        return d.name;
    } else if (key == "type") {
        return std::string(FGPositioned::nameForType(d.type));
    } else if (key == "distanceNm") {
        return d.distanceNm;
    }

    return {};
}
