// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileComment: stream log messages over a websocket
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include <memory>
#include <string>

#include "Websocket.hxx"

namespace flightgear {
namespace http {

/**
 * Streams log messages to a websocket client, at whatever verbosity that
 * client asked for. Each connection has its own levels, so several clients
 * can watch at once. A log callback is only registered with the logging
 * system while a connection is open, and the extra verbosity it requested
 * goes away again when it closes.
 *
 * The levels are a spec as understood by simgear::parseLogSpecFromString
 * (eg "info", "warn,nasal=debug"), given either in the 'levels' query
 * parameter when connecting, or by sending a JSON message at any time:
 *
 *   {"levels": "info,network=debug", "match": "text to search for"}
 *
 * The server replies with {"configured": {...}} or {"error": "..."}.
 * If no levels are given, 'info' is used. Log messages arrive as
 *
 *   {"entries": [{"timestamp": <unix seconds>, "level": "INFO",
 *                 "class": "nasal", "message": "..."}], "dropped": <count>}
 *
 * where "dropped" is only present if the client was too slow, and some
 * messages were discarded.
 */
class LogWebsocket : public Websocket
{
public:
    LogWebsocket(const std::string& levels, const std::string& match);
    ~LogWebsocket() override;

    void close() override;
    void handleRequest(const HTTPRequest& request, WebsocketWriter& writer) override;
    void poll(WebsocketWriter& writer) override;

private:
    class Callback;

    // returns an error message, or empty on success
    std::string configure(const std::string& levels, const std::string& match);
    std::string configuredReply() const;

    std::unique_ptr<Callback> _callback;
    std::string _tag;
    std::string _levels; // as parsed, in canonical form
    std::string _match;
    bool _registered = false;
    std::string _pendingReply; // sent at the next poll
};

} // namespace http
} // namespace flightgear
