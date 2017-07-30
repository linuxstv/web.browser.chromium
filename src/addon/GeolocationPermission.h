#pragma once
/*
 *      Copyright (C) 2015-2017 Team KODI
 *      http:/kodi.tv
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "include/cef_geolocation_handler.h"

#include <vector>

class CWebBrowserGeolocationPermission : public CefGeolocationHandler
{
public:
  CWebBrowserGeolocationPermission();

  virtual bool OnRequestGeolocationPermission(CefRefPtr<CefBrowser> browser, const CefString& requesting_url,
                                              int request_id, CefRefPtr<CefGeolocationCallback> callback) override;
  virtual void OnCancelGeolocationPermission(CefRefPtr<CefBrowser> browser, int request_id) override { }

  void ResetGeolocationPermission();

private:
  IMPLEMENT_REFCOUNTING(CWebBrowserGeolocationPermission);
  DISALLOW_COPY_AND_ASSIGN(CWebBrowserGeolocationPermission);

  bool LoadGeolocationPermission(bool initial);
  bool SaveGeolocationPermission();

  std::vector<std::string> m_allowedSides;
  std::vector<std::string> m_blockedSides;
};
