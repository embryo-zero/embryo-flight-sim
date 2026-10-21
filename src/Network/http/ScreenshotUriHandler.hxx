// ScreenshotUriHandler.hxx -- Provide screenshots via http
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Torsen Dreyer

#pragma once

#include "urihandler.hxx"

namespace flightgear {
namespace http {

class ScreenshotUriHandler : public URIHandler {
public:
  ScreenshotUriHandler( const std::string& uri = "/screenshot/" );
  ~ScreenshotUriHandler();
  virtual bool handleGetRequest( const HTTPRequest & request, HTTPResponse & response, Connection * connection );
  virtual bool poll( Connection * connection );
};

} // namespace http
} // namespace flightgear
