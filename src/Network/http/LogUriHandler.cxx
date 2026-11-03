// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileComment: HTTP access to the most recent log entries
// SPDX-FileCopyrightText: 2026 James Turner

#include "config.h"

#include "LogUriHandler.hxx"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <simgear/debug/debug_types.h>
#include <simgear/debug/logstream.hxx>
#include <simgear/misc/strutils.hxx>

using std::string;
using njson = nlohmann::json;

namespace flightgear {
namespace http {

namespace {

constexpr size_t MAX_RESULT_ENTRIES = 5000;
constexpr size_t DEFAULT_RESULT_COUNT = 100;

// what we need of a retained entry once the log thread is released again
struct LogEntry {
    uint64_t id = 0;
    double time = 0.0;
    string level;
    string debugClass;
    string file;
    int line = 0;
    string message;
};

// rank priorities so 'level' filtering behaves sensibly for the special values
int priorityRank(sgDebugPriority p)
{
    switch (p) {
    case SG_BULK: return 1;
    case SG_DEBUG: return 2;
    case SG_INFO:
    case SG_MANDATORY_INFO: return 3;
    case SG_WARN:
    case SG_DEV_WARN: return 4;
    case SG_ALERT:
    case SG_DEV_ALERT: return 5;
    case SG_POPUP: return 6;
    default: return 0;
    }
}

void sendError(HTTPResponse& response, int status, const string& message)
{
    response.StatusCode = status;
    response.Header["Content-Type"] = "text/plain";
    response.Content = message;
}

} // namespace

LogUriHandler::LogUriHandler(const std::string& uri)
    : URIHandler(uri)
{
}

LogUriHandler::~LogUriHandler() = default;

bool LogUriHandler::handleRequest(const HTTPRequest& request, HTTPResponse& response, Connection*)
{
    size_t count = DEFAULT_RESULT_COUNT;
    const string n = request.RequestVariables.get("n");
    if (!n.empty()) {
        count = std::clamp<long>(std::stol(n, nullptr, 10), 1, MAX_RESULT_ENTRIES);
    }

    int minRank = 0;
    const string level = request.RequestVariables.get("level");
    if (!level.empty()) {
        try {
            minRank = priorityRank(simgear::priorityFromString(level));
        } catch (const std::invalid_argument&) {
            minRank = 0;
        }
        if (minRank == 0) {
            sendError(response, 400, "unknown level '" + level + "' (use bulk, debug, info, warn, alert, popup)");
            return true;
        }
    }

    std::vector<sgDebugClass> classes;
    const string classArg = request.RequestVariables.get("class");
    if (!classArg.empty()) {
        for (const auto& c : simgear::strutils::split(classArg, ",")) {
            try {
                classes.push_back(simgear::debugClassFromString(simgear::strutils::strip(c)));
            } catch (std::exception&) {
                sendError(response, 400, "unknown log class '" + c + "'");
                return true;
            }
        }
    }

    const string match = simgear::strutils::lowercase(request.RequestVariables.get("match"));

    const string sinceArg = request.RequestVariables.get("since");
    const bool haveSince = !sinceArg.empty();
    const uint64_t since = haveSince ? std::stoull(sinceArg, nullptr, 10) : 0;

    if (sglog().maxRetainedEntries() == 0) {
        sendError(response, 503, "log buffering is off (set /sim/logging/buffer-entries above zero)");
        return true;
    }

    // filter, keeping the most recent 'count' matches newer than 'after'
    std::deque<LogEntry> selected;
    uint64_t newest = 0;
    auto collect = [&](uint64_t after) {
        selected.clear();
        sglog().visitRetainedEntries([&](const simgear::RetainedLogEntry& r) {
            newest = r.sequence;
            const auto& e = r.entry;
            if (r.sequence <= after) {
                return;
            }

            if (priorityRank(e.debugPriority) < minRank) {
                return;
            }

            if (!classes.empty() && std::find(classes.begin(), classes.end(), e.debugClass) == classes.end()) {
                return;
            }

            if (!match.empty() && simgear::strutils::lowercase(e.message).find(match) == string::npos) {
                return;
            }

            LogEntry out;
            out.id = r.sequence;
            out.time = r.elapsedSeconds;
            out.level = debugPriorityToString(e.debugPriority);
            out.debugClass = debugClassToString(e.debugClass);
            out.file = e.file ? e.file : "";
            // simgear flags "don't print file:line" by negating the line
            out.line = std::abs(e.line);
            out.message = e.message;
            selected.push_back(std::move(out));
            if (selected.size() > count) {
                selected.pop_front();
            }
        });
    };

    collect(since);

    // a cursor ahead of everything we hold can't be one of ours (it came from
    // another session), so start again from what there is
    const bool reset = haveSince && (since > newest);
    if (reset) {
        collect(0);
    }

    if (request.RequestVariables.get("format") == "text") {
        std::ostringstream os;
        for (const auto& e : selected) {
            os << e.id << " " << e.time << " [" << e.level << "]:" << e.debugClass << " " << e.message << "\n";
        }
        response.Header["Content-Type"] = "text/plain; charset=utf-8";
        response.Content = os.str();
        return true;
    }

    njson out;
    out["last_id"] = newest;
    if (reset) {
        out["reset"] = true;
    }
    out["entries"] = njson::array();
    for (const auto& e : selected) {
        njson j = {{"id", e.id},
                   {"time", e.time},
                   {"level", e.level},
                   {"class", e.debugClass},
                   {"message", e.message}};
        if (!e.file.empty()) {
            j["file"] = e.file;
            j["line"] = e.line;
        }
        out["entries"].push_back(std::move(j));
    }

    response.Header["Content-Type"] = "application/json";
    response.Content = out.dump(-1, ' ', false, njson::error_handler_t::replace);
    return true;
}

} // namespace http
} // namespace flightgear
