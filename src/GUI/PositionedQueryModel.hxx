// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <simgear/math/SGGeod.hxx>

#include <GUI/ItemModel.hxx>
#include <Navaids/positioned.hxx>

/**
 * @brief Model of navaids (VOR, NDB) and waypoints (FIXes) matching a search term.
 *
 * The search term is matched against the start of the ident, and the start of the
 * name (where one exists). Results are sorted by distance from a reference
 * position, which defaults to the current aircraft position.
 *
 * Keys: 'ident', 'name', 'label', 'value' (the PositionedID), 'type' and
 * 'distanceNm'. The label is 'ident (name)' for navaids, and 'ident' for waypoints.
 */
class PositionedQueryModel : public ItemModel
{
public:
    PositionedQueryModel();

    std::string searchTerm() const
    {
        return _searchTerm;
    }

    /**
     * @brief Set the search term: part of the ident or name. Terms shorter than
     * three characters produce no results.
     */
    void setSearchTerm(const std::string& search);

    /// include VOR and NDB navaids in the results (default true)
    void setQueryNavaids(bool b);

    /// include VORs / NDBs individually (both default true)
    void setQueryVOR(bool b);
    void setQueryNDB(bool b);

    /// include waypoints / fixes in the results (default true)
    void setQueryWaypoints(bool b);

    /**
     * @brief Set the position results are sorted relative to.
     */
    void setPosition(const SGGeod& pos);
    void setPositionLatLon(double latDeg, double lonDeg);

    /// revert to using the current aircraft position
    void clearPosition();

    size_t count() const override;
    std::any dataAt(size_t index, const std::string& key) const override;

private:
    struct Entry {
        PositionedID id = 0;
        std::string ident;
        std::string name;
        FGPositioned::Type type = FGPositioned::INVALID;
        double distanceNm = 0.0;
    };

    void requery();
    SGGeod referencePosition() const;

    std::string _searchTerm;
    bool _queryVOR = true;
    bool _queryNDB = true;
    bool _queryWaypoints = true;
    std::optional<SGGeod> _position;

    std::vector<Entry> _data;
};
