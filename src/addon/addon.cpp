/*
*  Copyright (C) 2015-2020 Alwin Esch (Team Kodi)
*  This file is part of Kodi - https://kodi.tv
*
*  SPDX-License-Identifier: GPL-3.0-or-later
*  See LICENSES/README.md for more information.
*/

#include "addon.h"

#include "AppBrowser.h"
#include "MessageIds.h"
#include "RequestContextHandler.h"
#include "SandboxControl.h"
#include "WebBrowserClient.h"
#include "include/cef_app.h"
#include "include/cef_version.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#ifdef WIN32
#include "include/cef_sandbox_win.h"
#endif
#ifndef WIN32
#include "include/wrapper/cef_library_loader.h"
#endif
#include "include/views/cef_textfield.h"

#include <chrono>
#include <kodi/Filesystem.h>
#include <kodi/gui/dialogs/FileBrowser.h>
#include <kodi/gui/dialogs/OK.h>
#include <thread>

std::atomic_int CWebBrowser::m_iUniqueClientId{0};

/*
 * StartInstance() is called from Kodi by other thread as main.
 * Here becomes the set of data done and the user asked by Dialogs for mandatory
 * settings.
 */
WEB_ADDON_ERROR CWebBrowser::StartInstance()
{
  kodi::Log(ADDON_LOG_INFO, "CWebBrowser::%s: Creating the Google Chromium Internet Browser add-on", __func__);

#if defined(TARGET_LINUX)
  // Load CEF library by self
  std::string cefLib = kodi::GetAddonPath(LIBRARY_PREFIX "cef" LIBRARY_SUFFIX);
  if (!cef_load_library(cefLib.c_str()))
  {
    kodi::Log(ADDON_LOG_ERROR, "CWebBrowser::%s: Failed to load CEF library '%s'", __func__, cefLib.c_str());
    return WEB_ADDON_ERROR_FAILED;
  }
#elif defined(TARGET_DARWIN)
  std::string cefLib = kodi::GetAddonPath(
      "Contents/Frameworks/Chromium Embedded Framework.framework/Chromium Embedded Framework");
  if (!m_cefLibraryLoader.LoadInMain(cefLib))
  {
    kodi::Log(ADDON_LOG_ERROR, "CWebBrowser::%s: Failed to load CEF library '%s'", __func__, cefLib.c_str());
    return WEB_ADDON_ERROR_FAILED;
  }
#endif

#if defined(CEF_USE_SANDBOX)
  // Check set of sandbox and if needed ask user about root password to set correct rights of them
  if (CefSandboxNeedRoot() && !SandboxControl::SetSandbox())
    return WEB_ADDON_ERROR_FAILED;
#endif

  // Set download path if not available
  if (kodi::GetSettingString("downloads.path").empty())
  {
    kodi::gui::dialogs::OK::ShowAndGetInput(kodi::GetLocalizedString(30080),
                                            kodi::GetLocalizedString(30081));

    std::string path;
    while (path.empty())
      kodi::gui::dialogs::FileBrowser::ShowAndGetDirectory("local", kodi::GetLocalizedString(30081),
                                                           path, true);

    kodi::SetSettingString("downloads.path", path);
  }

  // Initialize DRM widevine
  m_widewineControl.InitializeWidevine();

  std::string language = kodi::GetLanguage(LANG_FMT_ISO_639_1, true);

  // Create and delete CefSettings itself, otherwise comes seqfault during
  // "CefSettingsTraits::clear" call on destruction of CWebBrowser
  m_cefSettings = new CefSettings;
#if defined(TARGET_DARWIN)
  m_browserSubprocessPath = kodi::GetAddonPath(
      "Contents/Frameworks/kodichromium Helper.app/Contents/MacOS/kodichromium Helper");
  m_frameworkDirPath =
      kodi::GetAddonPath("Contents/Frameworks/Chromium Embedded Framework.framework/");
  m_resourcesPath =
      kodi::GetAddonPath("Contents/Frameworks/Chromium Embedded Framework.framework/Resources/");
  m_localesPath =
      kodi::GetAddonPath("Contents/Frameworks/Chromium Embedded Framework.framework/Resources/");

  m_cefSettings->no_sandbox = true; // Currently not work on Mac
#else
  m_browserSubprocessPath = CInstanceWeb::AddonLibPath("kodichromium");
  m_frameworkDirPath = CInstanceWeb::AddonLibPath();
  m_resourcesPath = CInstanceWeb::AddonSharePath("resources/");
  m_localesPath = CInstanceWeb::AddonSharePath("resources/locales/");

  m_cefSettings->no_sandbox = false;
#endif
  CefString(&m_cefSettings->browser_subprocess_path) = m_browserSubprocessPath;
  CefString(&m_cefSettings->framework_dir_path) = m_frameworkDirPath;
  CefString(&m_cefSettings->resources_dir_path) = m_resourcesPath;
  CefString(&m_cefSettings->locales_dir_path) = m_localesPath;
  m_cefSettings->multi_threaded_message_loop = false;
  m_cefSettings->external_message_pump = true;
  m_cefSettings->windowless_rendering_enabled = true;
  m_cefSettings->command_line_args_disabled = false;
  CefString(&m_cefSettings->cache_path) = kodi::GetBaseUserPath("pchHTMLCache");
  CefString(&m_cefSettings->user_data_path) = kodi::GetBaseUserPath();
  m_cefSettings->persist_session_cookies = false;
  m_cefSettings->persist_user_preferences = false;
  CefString(&m_cefSettings->product_version) =
      StringUtils::Format("Kodi/%s Chrome/%d.%d.%d.%d", KODICHROMIUM_VERSION, CHROME_VERSION_MAJOR,
                          CHROME_VERSION_MINOR, CHROME_VERSION_BUILD, CHROME_VERSION_PATCH);
  CefString(&m_cefSettings->locale) = language;
  CefString(&m_cefSettings->log_file) = "";
  m_cefSettings->log_severity =
      static_cast<cef_log_severity_t>(kodi::GetSettingInt("system.loglevelcef"));
  CefString(&m_cefSettings->javascript_flags) = kodi::GetTempAddonPath("chromium.log");
  m_cefSettings->pack_loading_disabled = false;
  m_cefSettings->remote_debugging_port = 8457;
  m_cefSettings->uncaught_exception_stack_size =
      kodi::GetSettingInt("system.uncaught_exception_stack_size");
  m_cefSettings->ignore_certificate_errors = false;
  m_cefSettings->background_color = 0;
  CefString(&m_cefSettings->accept_language_list) = language;

  kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Started web browser add-on process", __func__);

  m_app = new CClientAppBrowser(*this);
  m_audioHandler = new CAudioHandler(this, IsMuted());
  m_started = true;
  return WEB_ADDON_ERROR_NO_ERROR;
}

/*
 * StopInstance() is called from Kodi by other thread as main.
 */
void CWebBrowser::StopInstance()
{
  if (!m_started)
    return;

  m_started = false;
  m_audioHandler = nullptr;
  m_app = nullptr;

  m_widewineControl.DeinitializeWidevine();

  // deleted the created settings class
  m_cefSettings->Reset();
  delete m_cefSettings;
  m_cefSettings = nullptr;

#if defined(TARGET_LINUX)
  // unload cef library
  if (!cef_unload_library())
  {
    kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Failed to unload CEF library", __func__);
    return;
  }
#endif
}

/*
 * MainInitialize(), MainLoop() and MainShutdown() are called from Kodi main
 * thread!
 *
 * Due to special thread checks inside Chromium is it no more possible to run
 * over different thread as main.
 *
 * Further is the for rendering done stream also mandatory to use main thread.
 * It use on Direct X shared textures between two processes and CEF brings
 * soon a shared memory usage to give GL rendering from sandbox to this.
 */

bool CWebBrowser::MainInitialize()
{
  if (!m_started)
    return false;

  // #ifndef WIN32
  //   const char* cmdLine[3];
  //   cmdLine[0] = "";
  //   cmdLine[1] = "--disable-gpu";
  //   cmdLine[2] = "--disable-software-rasterizer";
  //   CefMainArgs args(3, (char**)cmdLine);
  // #else
  CefMainArgs args;
  // #endif
  if (!CefInitialize(args, *m_cefSettings, m_app.get(), nullptr))
  {
    kodi::Log(ADDON_LOG_ERROR, "CWebBrowser::%s: Web browser start failed", __func__);
    return false;
  }

  return true;
}

void CWebBrowser::MainLoop()
{
  if (!m_started)
    return;

  // Do CEF's message loop work
  CefDoMessageLoopWork();
}

void CWebBrowser::MainShutdown()
{
  if (!m_started)
    return;

  kodi::Log(ADDON_LOG_DEBUG, "Active clients during shutdown %li", m_browserClients.size());
  kodi::Log(ADDON_LOG_DEBUG, "Inactive clients during shutdown %li",
            m_browserClientsInactive.size());
  kodi::Log(ADDON_LOG_DEBUG, "Clients in delete process during shutdown start %li",
            m_browserClientsInDelete.size());

  if (!m_browserClients.empty() || !m_browserClientsInactive.empty())
    kodi::Log(ADDON_LOG_FATAL,
              "Still browser clients in use during shutdown (active: %li, inactive: %li",
              m_browserClients.size(), m_browserClientsInactive.size());

  // Wait until all clients are deleted otherwise can CefShutdown() not work right!
  int tries = 1000;
  while (!m_browserClientsInDelete.empty() && tries-- > 0)
  {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  // Do last process works, to confirm everything done
  CefDoMessageLoopWork();

  // shutdown CEF
  size_t size = m_browserClientsInDelete.size();
  if (size == 0)
    CefShutdown();
  else
    kodi::Log(ADDON_LOG_FATAL, "Still %li browsers not deleted! CefShutdown() becomes not called",
              size);
}

void CWebBrowser::InformDestroyed(int uniqueClientId)
{
  m_browserClientsInDelete.erase(uniqueClientId);
}

void CWebBrowser::SetMute(bool mute)
{
  if (m_audioHandler && m_started)
    m_audioHandler->SetMute(mute);
}

bool CWebBrowser::SetLanguage(const char* language)
{
  if (!m_started)
    return false;

  kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Web browser language set to '%s'", __func__, language);
  return true;
}

kodi::addon::CWebControl* CWebBrowser::CreateControl(const std::string& sourceName,
                                                     const std::string& startURL,
                                                     KODI_HANDLE handle)
{
  CEF_REQUIRE_UI_THREAD();

  if (!m_started)
    return nullptr;

  kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Web browser control creation started", __func__);

  CefRefPtr<CWebBrowserClient> browserClient;

  std::lock_guard<std::mutex> lock(m_mutex);

  auto itr = m_browserClientsInactive.find(sourceName);
  if (itr != m_browserClientsInactive.end())
  {
    kodi::Log(ADDON_LOG_INFO, "CWebBrowser::%s: Found control in inactive mode and setting active", __func__);
    browserClient = itr->second;
    browserClient->SetActive();
    m_browserClientsInactive.erase(itr);
  }
  else
  {
    for (const auto& entry : m_browserClients)
    {
      if (entry.second->GetName() == sourceName)
      {
        return entry.second;
      }
    }

    CefRefPtr<CRequestContextHandler> contextHandler = new CRequestContextHandler;
    browserClient =
        new CWebBrowserClient(handle, m_iUniqueClientId++, startURL, this, contextHandler);
    contextHandler->Init(browserClient);

    CefWindowInfo info;
    info.SetAsWindowless(kNullWindowHandle);
#ifdef WIN32
    info.shared_texture_enabled = true;
    info.external_begin_frame_enabled = false;
#endif // WIN32

    CefBrowserSettings settings;
    //TODO Check CefBrowserHost::SetWindowlessFrameRate(...) usable for streams?
    settings.windowless_frame_rate = static_cast<int>(browserClient->GetFPS());
    CefString(&settings.standard_font_family) = "";
    CefString(&settings.fixed_font_family) = "";
    CefString(&settings.serif_font_family) = "";
    CefString(&settings.sans_serif_font_family) = "";
    CefString(&settings.cursive_font_family) = "";
    CefString(&settings.fantasy_font_family) = "";
    settings.default_font_size = 0;
    settings.default_fixed_font_size = 0;
    settings.minimum_font_size = 0;
    settings.minimum_logical_font_size = 0;
    CefString(&settings.default_encoding) = ""; // "ISO-8859-1" if empty
    settings.remote_fonts = STATE_DEFAULT;
    settings.javascript = STATE_ENABLED;
    settings.javascript_close_windows = STATE_DEFAULT;
    settings.javascript_access_clipboard = STATE_DEFAULT;
    settings.javascript_dom_paste = STATE_DEFAULT;
    settings.plugins = STATE_ENABLED;
    settings.universal_access_from_file_urls = STATE_DEFAULT;
    settings.file_access_from_file_urls = STATE_DEFAULT;
    settings.web_security = STATE_DEFAULT;
    settings.image_loading = STATE_DEFAULT;
    settings.image_shrink_standalone_to_fit = STATE_DEFAULT;
    settings.text_area_resize = STATE_DEFAULT;
    settings.tab_to_links = STATE_DEFAULT;
    settings.local_storage = STATE_DEFAULT;
    settings.databases = STATE_DEFAULT;
    settings.application_cache = STATE_DEFAULT;
    settings.webgl = STATE_ENABLED;
    settings.background_color = 0x00; // fully transparent
    CefString(&settings.accept_language_list) = "";

    CefRefPtr<CefDictionaryValue> extra_info = CefDictionaryValue::Create();
    extra_info->SetInt(SettingValues::security_webaddon_access,
                       kodi::GetSettingInt("security.webaddon.access"));
    CefRefPtr<CefRequestContext> request_context =
        CefRequestContext::CreateContext(CefRequestContext::GetGlobalContext(), contextHandler);
    if (!CefBrowserHost::CreateBrowser(info, browserClient, "", settings, extra_info,
                                       request_context))
    {
      kodi::Log(ADDON_LOG_ERROR, "CWebBrowser::%s: Web browser creation failed", __func__);
      if (browserClient)
      {
        contextHandler->Clear();
        browserClient = nullptr;
      }
      return nullptr;
    }
  }

  int uniqueId = browserClient->GetUniqueId();
  m_browserClients.emplace(uniqueId, browserClient);
  kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Web browser control created", __func__);
  return browserClient.get();
}

bool CWebBrowser::DestroyControl(kodi::addon::CWebControl* control, bool complete)
{
  if (!m_started)
    return false;

  //! Check for wrongly passed empty handle.
  CefRefPtr<CWebBrowserClient> browserClient = static_cast<CWebBrowserClient*>(control);
  if (browserClient == nullptr)
  {
    kodi::Log(ADDON_LOG_ERROR, "CWebBrowser::%s: Web browser control destroy called without handle!", __func__);
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  const auto& itr = m_browserClients.find(browserClient->GetDataIdentifier());
  if (itr != m_browserClients.end())
    m_browserClients.erase(itr);
  browserClient->SetInactive();

  if (complete)
  {
    kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Web browser control destroy complete", __func__);
    const auto& inactiveClient = m_browserClientsInactive.find(browserClient->GetName());
    if (inactiveClient != m_browserClientsInactive.end())
      m_browserClientsInactive.erase(inactiveClient);

    m_browserClientsInDelete.insert(browserClient->GetUniqueId());
    browserClient->CloseComplete();
    CefDoMessageLoopWork();
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Web browser control destroy to set inactive", __func__);
    if (itr == m_browserClients.end())
    {
      kodi::Log(ADDON_LOG_ERROR, "CWebBrowser::%s: Web browser control destroy called for invalid id '%i'",
                __func__, browserClient->GetDataIdentifier());
      return false;
    }
    m_browserClientsInactive[browserClient->GetName()] = browserClient;
  }

  kodi::Log(ADDON_LOG_DEBUG, "CWebBrowser::%s: Web browser control destroy done", __func__);
  return true;
}

ADDONCREATOR(CWebBrowser)
