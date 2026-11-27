//-------------------------------------------------------------------------------------------------
// /Wallpaper/ModuleEntry.cpp
// The nModules Project
//
// nWallpaper entry points.
//
// RC:    ThemeWallpaper "<file>"        the theme's own wallpaper; without it the user's
//                                       choice in Windows is left alone (and restored)
//        ThemeWallpaperStyle <style>     Center|Tile|Stretch|Fit|Fill|Span
// Bangs: !SetWallpaper "<file>" [style]   !SetWallpaperStyle <style>
//
// Without Explorer (LiteStep as the session's shell) it also paints the desktop's
// background; see DesktopBackground.cpp.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include "Bangs.h"
#include "DesktopBackground.h"
#include "ThemeWallpaper.h"
#include "Version.h"

#include <strsafe.h>

// The LSModule class
LSModule gLSModule(TEXT(MODULE_NAME), TEXT(MODULE_AUTHOR), MakeVersion(MODULE_VERSION));

// The messages we want from the core
const UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, 0 };


/// <summary>
/// Called by the LiteStep core when this module is loaded.
/// </summary>
EXPORT_CDECL(int) initModuleW(HWND parent, HINSTANCE instance, LPCWSTR /* path */) {
  if (!gLSModule.Initialize(parent, instance)) {
    return 1;
  }
  if (!gLSModule.ConnectToCore(MakeVersion(CORE_VERSION))) {
    return 1;
  }
  Bangs::_Register();
  ThemeWallpaper::Apply();
  DesktopBackground::Create();

  return 0;
}


/// <summary>
/// Called by the LiteStep core when this module is about to be unloaded.
/// </summary>
void quitModule(HINSTANCE /* instance */) {
  DesktopBackground::Destroy();
  Bangs::_Unregister();
  gLSModule.DeInitalize();
}


/// <summary>
/// Handles the main window's messages.
/// </summary>
/// <param name="window">The window the message is for.</param>
/// <param name="message">The type of message.</param>
/// <param name="wParam">wParam</param>
/// <param name="lParam">lParam</param>
LRESULT WINAPI LSMessageHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  using LiteStep::GetLitestepWnd;
  switch (message) {
  case WM_CREATE:
    SendMessage(GetLitestepWnd(), LM_REGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
    return 0;

  case WM_DESTROY:
    SendMessage(GetLitestepWnd(), LM_UNREGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
    return 0;

  case LM_REFRESH:
    ThemeWallpaper::Apply();
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

