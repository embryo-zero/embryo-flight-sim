// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileComment: stream log messages over a websocket
// SPDX-FileCopyrightText: 2026 James Turner

#include "config.h"

#include "LogWebsocket.hxx"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

#include <nlohmann/json.hpp>

#include <simgear/debug/LogCallback.hxx>
#include <simgear/debug/logstream.hxx>
#include <simgear/misc/strutils.hxx>

using std::string;
using njson = nlohmann::json;

namespace flightgear {
namespace http {

namespace {

// a client which doesn't keep up loses the oldest messages, since the log
// thread must never be held up by it
constexpr size_t MAX_QUEUED_ENTRIES = 2000;
constexpr size_t MAX_ENTRIES_PER_MESSAGE = 500;

const char* DEFAULT_LEVELS = "info";

struct QueuedEntry {
    double timestamp;
    string level;
    string debugClass;
    string message;
};

std::atomic<unsigned int> nextConnectionId{1};

string dumpJson(const njson& j)
{
    return j.dump(-1, ' ', false, njson::error_handler_t::replace);
}

} // namespace

// Runs on the shared log thread, so only filters and queues. The websocket's
// own thread drains it.
class LogWebsocket::Callback : public simgear::LogCallback
{
public:
    explicit Callback(const string& tag)
        : simgear::LogCallback(tag)
    {
    }

    bool doProcessEntry(const simgear::LogEntry& e) override
    {
        if (!shouldLog(e.debugClass, e.debugPriority)) {
            return true; // handled (ignored)
        }

        std::lock_guard<std::mutex> g(_mutex);
        if (!_match.empty() && simgear::strutils::lowercase(e.message).find(_match) == string::npos) {
            return true;
        }

        const auto now = std::chrono::system_clock::now().time_since_epoch();
        _queue.push_back({std::chrono::duration<double>(now).count(),
                          debugPriorityToString(e.debugPriority),
                          debugClassToString(e.debugClass),
                          e.message});
        if (_queue.size() > MAX_QUEUED_ENTRIES) {
            _queue.pop_front();
            ++_dropped;
        }
        return true;
    }

    void setMatch(const string& match)
    {
        std::lock_guard<std::mutex> g(_mutex);
        _match = simgear::strutils::lowercase(match);
    }

    // takes up to 'max' queued entries, and the count of discarded ones
    void drain(std::vector<QueuedEntry>& entries, size_t max, uint64_t& dropped)
    {
        std::lock_guard<std::mutex> g(_mutex);
        while (!_queue.empty() && entries.size() < max) {
            entries.push_back(std::move(_queue.front()));
            _queue.pop_front();
        }
        dropped = _dropped;
        _dropped = 0;
    }

private:
    std::mutex _mutex;
    std::deque<QueuedEntry> _queue;
    uint64_t _dropped = 0;
    string _match; // lowercase
};

LogWebsocket::LogWebsocket(const std::string& levels, const std::string& match)
    : _tag("http-log-ws-" + std::to_string(nextConnectionId++))
{
    _callback = std::make_unique<Callback>(_tag);

    string error = configure(levels, match);
    if (!error.empty()) {
        // keep going with the default levels, and tell the client
        _pendingReply = dumpJson({{"error", error}});
        configure(DEFAULT_LEVELS, match);
    } else {
        _pendingReply = configuredReply();
    }

    // the levels were set before this, so what the logging system generates
    // is right straight away. Streams new messages only, not the retained history
    sglog().addCallback(_callback.get(), false);
    _registered = true;
}

LogWebsocket::~LogWebsocket()
{
    close();
}

void LogWebsocket::close()
{
    if (_registered) {
        sglog().removeCallback(_callback.get());
        _registered = false;
    }
}

string LogWebsocket::configure(const string& levels, const string& match)
{
    const string spec = levels.empty() ? string(DEFAULT_LEVELS) : levels;
    const auto parsed = simgear::parseLogSpecFromString(spec);
    if (!parsed) {
        return "unable to parse log levels '" + spec + "' (use eg 'info' or 'warn,nasal=debug')";
    }

    if (_registered) {
        // goes via the log stream, so what is generated follows
        sglog().setLogLevels(*parsed, _tag);
    } else {
        _callback->setLogLevels(*parsed);
    }
    _callback->setMatch(match);
    _levels = parsed->to_string();
    _match = match;
    return string();
}

string LogWebsocket::configuredReply() const
{
    return dumpJson({{"configured", {{"levels", _levels}, {"match", _match}}}});
}

void LogWebsocket::handleRequest(const HTTPRequest& request, WebsocketWriter& writer)
{
    njson msg = njson::parse(request.Content, nullptr, false);
    if (!msg.is_object()) {
        writer.writeText(dumpJson({{"error", "expected a JSON object, eg {\"levels\": \"warn,nasal=debug\"}"}}));
        return;
    }

    // anything not mentioned stays as it was
    string error;
    try {
        error = configure(msg.value("levels", _levels), msg.value("match", _match));
    } catch (njson::exception&) {
        error = "'levels' and 'match' must be strings";
    }
    writer.writeText(error.empty() ? configuredReply() : dumpJson({{"error", error}}));
}

void LogWebsocket::poll(WebsocketWriter& writer)
{
    if (!_pendingReply.empty()) {
        writer.writeText(_pendingReply);
        _pendingReply.clear();
    }

    std::vector<QueuedEntry> entries;
    uint64_t dropped = 0;
    _callback->drain(entries, MAX_ENTRIES_PER_MESSAGE, dropped);
    if (entries.empty() && dropped == 0) {
        return;
    }

    njson out;
    out["entries"] = njson::array();
    for (const auto& e : entries) {
        out["entries"].push_back({{"timestamp", e.timestamp},
                                  {"level", e.level},
                                  {"class", e.debugClass},
                                  {"message", e.message}});
    }
    if (dropped > 0) {
        out["dropped"] = dropped;
    }
    writer.writeText(dumpJson(out));
}

} // namespace http
} // namespace flightgear
