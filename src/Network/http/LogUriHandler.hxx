// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileComment: HTTP access to the most recent log entries
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include "urihandler.hxx"

namespace flightgear {
namespace http {

/**
 * Returns recent log entries from the logstream's in-memory ring of retained
 * entries, whose size is /sim/logging/buffer-entries (503 if that is zero).
 *
 * What is available depends on what the logstream generates - by default info
 * and above, or whatever --log-level asked for. Use the /LogListener websocket
 * to see more verbose messages from a running session.
 *
 * Query arguments (all optional):
 *   n       - maximum number of entries to return (default 100, most recent)
 *   level   - minimum priority: bulk, debug, info, warn, alert, popup
 *   class   - comma-separated log class names, eg 'nasal,network'
 *   match   - only entries whose message contains this text (case-insensitive)
 *   since   - return only entries after this cursor: pass back the 'last_id'
 *             of an earlier response (entry ids increase by one per entry)
 *   format  - 'json' (default) or 'text'
 *
 * Each JSON entry has id, time (seconds since startup), level, class, message,
 * and the source file and line where it was logged. If 'since' is ahead of the
 * newest entry (so is stale), the response has "reset": true and everything
 * held is considered.
 */
class LogUriHandler : public URIHandler
{
public:
    LogUriHandler(const std::string& uri = "/log");
    ~LogUriHandler() override;

    bool handleRequest(const HTTPRequest& request, HTTPResponse& response, Connection* connection) override;
};

} // namespace http
} // namespace flightgear
