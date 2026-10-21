// DialogsUriHandler.hxx -- List the GUI dialogs
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include "urihandler.hxx"

namespace flightgear {
namespace http {

/**
 * Lists the GUI dialogs: GET /dialogs returns the open ones as JSON,
 * ?all=y includes every known dialog.
 */
class DialogsUriHandler : public URIHandler
{
public:
    DialogsUriHandler(const std::string& uri = "/dialogs")
        : URIHandler(uri)
    {
    }
    bool handleRequest(const HTTPRequest& request, HTTPResponse& response, Connection* connection) override;
};

} // namespace http
} // namespace flightgear
