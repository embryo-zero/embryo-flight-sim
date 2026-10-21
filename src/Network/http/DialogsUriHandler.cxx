// DialogsUriHandler.cxx -- List the GUI dialogs
// SPDX-FileCopyrightText: 2026 James Turner
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include "DialogsUriHandler.hxx"

#include <GUI/new_gui.hxx>
#include <Main/globals.hxx>
#include <nlohmann/json.hpp>

#include "HTTPRequest.hxx"
#include "HTTPResponse.hxx"

using njson = nlohmann::json;

namespace flightgear {
namespace http {

bool DialogsUriHandler::handleRequest(const HTTPRequest& request, HTTPResponse& response, Connection*)
{
    response.Header["Content-Type"] = "application/json; charset=UTF-8";

    if (request.Method != "GET") {
        response.Header["Allow"] = "GET";
        response.StatusCode = 405;
        response.Content = "{}";
        return true;
    }

    auto gui = globals->get_subsystem<NewGUI>();
    if (!gui) {
        response.StatusCode = 503;
        response.Content = R"({"error":"GUI subsystem not available"})";
        return true;
    }

    const bool all = request.RequestVariables.get("all") == "y";
    const auto names = all ? gui->knownDialogNames() : gui->openDialogNames();

    njson dialogs = njson::array();
    for (const auto& name : names) {
        const SGPath file = gui->dialogFilePath(name);
        dialogs.push_back({{"name", name},
                           {"open", gui->isDialogOpen(name)},
                           {"file", file.isNull() ? njson(nullptr) : njson(file.utf8Str())}});
    }

    response.Content = njson{{"dialogs", dialogs}}.dump(request.RequestVariables.get("i") == "y" ? 2 : -1);
    return true;
}

} // namespace http
} // namespace flightgear
