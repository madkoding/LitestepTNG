//-------------------------------------------------------------------------------------------------
// /Core/nCore.cpp
// The nModules Project
//
// Main .cpp file for the nCore module.
//-------------------------------------------------------------------------------------------------
#include "CoreMessages.h"
#include "ParsedText.hpp"
#include "TextFunctions.h"
#include "Version.h"

#include "../CoreCom/Core.h"
#include "FileSystemLoader.h"

#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/ErrorHandler.h"
#include "../ModuleKit/MonitorInfo.hpp"

#include "../ModuleKit/WindowBangs.h"
#include "../ModuleKit/StateBangs.h"
#include "../ModuleKit/BrushBangs.h"

#include "../Utilities/Versioning.h"

#include <strsafe.h>


// The messages we want from the core
static const UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, 0 };
HWND ghWndMsgHandler = nullptr;

// Class pointers
MonitorInfo gMonitorInfo;

// Constants
static const LPCWSTR gMsgHandler = L"LSnCore";

// When the [time] text function should send out change notifications.
UINT_PTR timeTimer;

// --- Explorer's taskbar, when LiteStep runs on top of Explorer -----------------
// "HideExplorerTaskbar true" keeps Explorer's taskbars hidden so the theme's own bars
// replace them. Explorer stays the real shell (UWP apps, Settings, virtual desktops
// keep working); we only hide its windows, and put them back when the module unloads.
// Explorer re-shows a taskbar now and then (new monitor, Start menu), hence the timer.
static bool sHideExplorerTaskbar = false;

static BOOL CALLBACK ExplorerTaskbarProc(HWND window, LPARAM show) {
  WCHAR className[64] = L"";
  GetClassNameW(window, className, _countof(className));
  if (_wcsicmp(className, L"Shell_TrayWnd") != 0 &&
      _wcsicmp(className, L"Shell_SecondaryTrayWnd") != 0) {
    return TRUE;
  }

  // LiteStep's tray service owns a window of the same class; leave it alone.
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process == GetCurrentProcessId()) {
    return TRUE;
  }

  // Async, so a busy Explorer cannot stall the shell thread.
  if (show) {
    if (!IsWindowVisible(window)) ShowWindowAsync(window, SW_SHOWNA);
  } else if (IsWindowVisible(window)) {
    ShowWindowAsync(window, SW_HIDE);
  }
  return TRUE;
}

static void ShowExplorerTaskbar(bool show) {
  EnumWindows(ExplorerTaskbarProc, show ? 1 : 0);
}


// Screen space for the theme's bars ("ReserveScreenTop" / "ReserveScreenBottom", pixels
// on the primary monitor). Explorer owns the work area and knows nothing about our bars:
// it reserves an edge for its own (hidden) taskbar and nothing for ours. We write the work
// area we want and write it again whenever Explorer recomputes it, which only happens on
// rare events (display change, taskbar settings). The change is deliberately NOT
// broadcast: Explorer reacts to that broadcast by recomputing its own value at once.
//
// Registering AppBars would be the textbook way, but SHAppBarMessage talks to the first
// Shell_TrayWnd it finds, and that is LiteStep's own tray service, not Explorer.
static int sReserve[2] = { 0, 0 }; // top, bottom
static bool sWorkAreaSaved = false;
static RECT sOriginalWorkArea;     // what Explorer had when we loaded

static bool DesiredWorkArea(RECT *desired) {
  POINT origin = { 0, 0 };
  MONITORINFO info = { sizeof(MONITORINFO) };
  if (!GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info)) {
    return false;
  }
  *desired = info.rcMonitor;
  desired->top += sReserve[0];
  desired->bottom -= sReserve[1];
  return desired->bottom > desired->top;
}

/// <summary>
/// Makes the primary work area the monitor minus our top and bottom strips.
/// Does nothing when it already is, so it is cheap to call from the timer.
/// </summary>
static void EnforceWorkArea() {
  RECT desired, current;
  if ((sReserve[0] <= 0 && sReserve[1] <= 0) || !DesiredWorkArea(&desired) ||
      !SystemParametersInfoW(SPI_GETWORKAREA, 0, &current, 0)) {
    return;
  }
  if (!sWorkAreaSaved) {
    sOriginalWorkArea = current;
    sWorkAreaSaved = true;
  }
  if (!EqualRect(&desired, &current)) {
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &desired, 0);
  }
}

static void RestoreWorkArea() {
  if (sWorkAreaSaved) {
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &sOriginalWorkArea, SPIF_SENDCHANGE);
    sWorkAreaSaved = false;
  }
}

// Service functions
EXPORT_CDECL(Window*) FindRegisteredWindow(LPCWSTR prefix);
extern void LoadCompleted(UINT64 id, LPVOID result);
extern void LoadItemCompleted(UINT64 id, LPVOID result);

extern void SendCoreMessage(UINT message, WPARAM, LPARAM);


/// <summary>
/// Gets the current core version.
/// </summary>
EXPORT_CDECL(VERSION) GetCoreVersion() {
  return MakeVersion(MODULE_VERSION);
}


/// <summary>
/// Gets current monitor information.
/// </summary>
EXPORT_CDECL(MonitorInfo&) FetchMonitorInfo() {
  return gMonitorInfo;
}


/// <summary>
/// Handles the main window's messages.
/// </summary>
/// <param name="hWnd">The window the message is for.</param>
/// <param name="uMsg">The type of message.</param>
/// <param name="wParam">wParam</param>
/// <param name="lParam">lParam</param>
static LRESULT WINAPI MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case LM_GETREVID:
    {
      size_t length;
      StringCchPrintfW((LPWSTR)lParam, 64, L"%s: ", TEXT(MODULE_NAME));
      length = lstrlenW((LPWSTR)lParam);
      GetVersionString(MakeVersion(MODULE_VERSION), (LPWSTR)lParam + length, 64 - length, false);

      if (SUCCEEDED(StringCchLengthW((LPWSTR)lParam, 64, &length))) {
        return length;
      }

      lParam = NULL;
    }
    return 0;

  case LM_REFRESH:
    TextFunctions::_Refresh();
    return 0;

  case WM_SETTINGCHANGE:
    if (wParam == SPI_SETWORKAREA) {
      gMonitorInfo.Update();
      SendCoreMessage(NCORE_DISPLAYCHANGE, wParam, lParam);
    }
    SendCoreMessage(NCORE_SETTINGCHANGE, wParam, lParam);
    return 0;

  case WM_DISPLAYCHANGE:
    gMonitorInfo.Update();
    SendCoreMessage(NCORE_DISPLAYCHANGE, wParam, lParam);
    return 0;

  case WM_TIMER:
    if (wParam == timeTimer) {
      DynamicTextChangeNotification(L"Time", 0);
      DynamicTextChangeNotification(L"Time", 1);
      DynamicTextChangeNotification(L"WindowTitle", 1);
      TextFunctions::_Sample();
      DynamicTextChangeNotification(L"CPU", 0);
      DynamicTextChangeNotification(L"Memory", 0);
      DynamicTextChangeNotification(L"MemoryUsedGB", 0);
      DynamicTextChangeNotification(L"NowPlaying", 0);
      DynamicTextChangeNotification(L"GPU", 0);
      DynamicTextChangeNotification(L"VRAM", 0);
      DynamicTextChangeNotification(L"VRAMUsedGB", 0);
      if (sHideExplorerTaskbar) {
        ShowExplorerTaskbar(false);
        EnforceWorkArea();
      }
    }
    return 0;

  case NCORE_FILE_SYSTEM_LOAD_COMPLETE:
    LoadCompleted(UINT64(wParam), LPVOID(lParam));
    return 0;

  case NCORE_FILE_SYSTEM_ITEM_LOAD_COMPLETE:
    LoadItemCompleted(UINT64(wParam), LPVOID(lParam));
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}


/// <summary>
/// Creates the main message handler.
/// </summary>
/// <param name="instance">The instance to attach this message handler to.</param>
static bool CreateMainWindow(HINSTANCE instance) {
  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(WNDCLASSEXW));
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = MainProc;
  wc.hInstance = instance;
  wc.lpszClassName = gMsgHandler;
  wc.style = CS_NOCLOSE;

  if (!RegisterClassExW(&wc)) {
    ErrorHandler::Error(ErrorHandler::Level::Critical, L"Failed to register nCore's window class!");
    return false;
  }

  ghWndMsgHandler = CreateWindowExW(WS_EX_TOOLWINDOW, gMsgHandler, L"", WS_POPUP,
    0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

  if (!ghWndMsgHandler) {
    ErrorHandler::Error(ErrorHandler::Level::Critical, L"Failed to create nCore's message handler!");
    UnregisterClassW(gMsgHandler, instance);
    return false;
  }

  // Let other modules get the instance using FindWindow + GetWindowLongPtr()
  SetWindowLongPtrW(ghWndMsgHandler, GWLP_USERDATA, (LONG_PTR)instance);
  SendMessageW(LiteStep::GetLitestepWnd(), LM_REGISTERMESSAGE, (WPARAM)ghWndMsgHandler, (LPARAM)gLSMessages);

  return true;
}


/// <summary>
/// Called by the LiteStep core when this module is loaded.
/// </summary>
/// <param name="parent"></param>
/// <param name="instance">Handle to this module's instance.</param>
/// <param name="path">Path to the LiteStep directory.</param>
/// <returns>0 on success, non-zero on error.</returns>
/// <remarks>
/// If this function returns non-zero, the module will be unloaded immediately, without
/// going through quitModule.
/// </remarks>
EXPORT_CDECL(int) initModuleW(HWND parent, HINSTANCE instance, LPCWSTR path) {
  UNREFERENCED_PARAMETER(parent);
  UNREFERENCED_PARAMETER(path);

  // Initialize
  if (!CreateMainWindow(instance)) {
    return 1;
  }

  TextFunctions::_Register();
  timeTimer = SetTimer(ghWndMsgHandler, 1, 1000, nullptr);

  sHideExplorerTaskbar = GetRCBoolDefW(L"HideExplorerTaskbar", FALSE) != FALSE;
  if (sHideExplorerTaskbar) {
    ShowExplorerTaskbar(false);
    sReserve[0] = LiteStep::GetRCInt(L"ReserveScreenTop", 0);
    sReserve[1] = LiteStep::GetRCInt(L"ReserveScreenBottom", 0);
    EnforceWorkArea();
  }

  // We need to be connected to the core for some of the functions in nShared to work... xD
  nCore::Connect(MakeVersion(MODULE_VERSION));

  // Register window bangs
  WindowBangs::Register(L"n", FindRegisteredWindow);
  StateBangs::Register(L"n", FindRegisteredWindow);
  BrushBangs::Register(L"n", FindRegisteredWindow);


  return 0;
}


/// <summary>
/// Called by the LiteStep core when this module is about to be unloaded.
/// </summary>
/// <param name="instance">Handle to this module's instance.</param>
EXPORT_CDECL(void) quitModule(HINSTANCE instance) {

  WindowBangs::UnRegister(L"n");
  StateBangs::UnRegister(L"n");
  BrushBangs::UnRegister(L"n");

  // Deinitalize
  if (ghWndMsgHandler) {
    KillTimer(ghWndMsgHandler, timeTimer);
    SendMessageW(LiteStep::GetLitestepWnd(), LM_UNREGISTERMESSAGE, (WPARAM)ghWndMsgHandler, (LPARAM)gLSMessages);
    DestroyWindow(ghWndMsgHandler);
  }

  TextFunctions::_Unregister();

  if (sHideExplorerTaskbar) {
    RestoreWorkArea();
    ShowExplorerTaskbar(true);
  }

  UnregisterClassW(gMsgHandler, instance);
}






