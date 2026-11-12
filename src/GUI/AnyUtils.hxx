// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>

/**
 * @file
 * @brief Helpers for working with std::any
 *
 * The set of numeric types mirrors those simgear's any_to_nasal_helper
 * supports, so anything which can reach Nasal can also be compared here.
 */

#pragma once

#include <any>
#include <cstdint>
#include <optional>
#include <string>

namespace flightgear::any_util {

/**
 * @brief extract any numeric (not bool) value as a double
 * 64-bit integers are exact up to 2^53, as for the Nasal conversion.
 */
inline std::optional<double> toNumber(const std::any& a)
{
    const auto& t = a.type();
    if (t == typeid(int)) return std::any_cast<int>(a);
    if (t == typeid(unsigned int)) return std::any_cast<unsigned int>(a);
    if (t == typeid(long)) return static_cast<double>(std::any_cast<long>(a));
    if (t == typeid(unsigned long)) return static_cast<double>(std::any_cast<unsigned long>(a));
    if (t == typeid(int64_t)) return static_cast<double>(std::any_cast<int64_t>(a));
    if (t == typeid(uint64_t)) return static_cast<double>(std::any_cast<uint64_t>(a));
    if (t == typeid(float)) return std::any_cast<float>(a);
    if (t == typeid(double)) return std::any_cast<double>(a);
    return std::nullopt;
}

/// extract a string value; other types (including numbers) are not converted
inline std::optional<std::string> toString(const std::any& a)
{
    if (const auto s = std::any_cast<std::string>(&a)) return *s;
    if (const auto c = std::any_cast<const char*>(&a)) return std::string(*c);
    return std::nullopt;
}

inline std::optional<bool> toBool(const std::any& a)
{
    if (const auto b = std::any_cast<bool>(&a)) return *b;
    return std::nullopt;
}

/**
 * @brief compare two values: numbers numerically (across types), strings and
 * bools by value. Empty values and mismatched kinds never match.
 */
inline bool valuesMatch(const std::any& a, const std::any& b)
{
    if (const auto na = toNumber(a)) {
        const auto nb = toNumber(b);
        return nb && (*na == *nb);
    }
    if (const auto sa = toString(a)) {
        const auto sb = toString(b);
        return sb && (*sa == *sb);
    }
    if (const auto ba = toBool(a)) {
        const auto bb = toBool(b);
        return bb && (*ba == *bb);
    }
    return false;
}

} // namespace flightgear::any_util
