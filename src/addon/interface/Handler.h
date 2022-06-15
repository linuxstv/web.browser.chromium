/*
 *  Copyright (C) 2015-2020 Alwin Esch (Team Kodi)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "include/wrapper/cef_message_router.h"

class CWebBrowserClient;

class CJSHandler : public CefMessageRouterBrowserSide::Handler
{
public:
  CJSHandler(CefRefPtr<CWebBrowserClient> client);
  virtual ~CJSHandler() = default;

  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64 query_id, const CefString& request,
               bool persistent, CefRefPtr<Callback> callback) override;
  void OnQueryCanceled(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64 query_id) override;

private:
  static bool ParseString(std::string& in, std::string& out, size_t& delim);

  static void OnQueryProcess(std::string url, int64 query_id,
                             std::string request, bool persistent, CefRefPtr<Callback> callback);

  CefRefPtr<CWebBrowserClient> m_client;
};
