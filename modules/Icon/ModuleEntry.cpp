//-------------------------------------------------------------------------------------------------
// /Icon/ModuleEntry.cpp
// The nModules Project
//
// LiteStep entry points for nIcon: groups of icons showing the content of a folder.
//
// RC:    *nIcon <prefix>                     one group; <prefix>Folder (default Desktop),
//                                            <prefix>Icon* (size, font), <prefix>Tile*, layout
//        <prefix>OnDesktop true              keep the group under every program's window
//        <prefix>ReplaceExplorerIcons true   while the group is visible, hide the desktop icons
//                                            Explorer draws itself (LiteStep running on top of it)
// Bangs: !DesktopIcons [show|hide|toggle]    for the groups that replace Explorer's icons
//-------------------------------------------------------------------------------------------------
#include "TileGroup.hpp"
#include "Version.h"

#include "../ModuleKit/ErrorHandler.h"
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include <string>
#include <unordered_map>

// The messages we want from the core
static const UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, LM_FULLSCREENACTIVATED,
LM_FULLSCREENDEACTIVATED, 0 };

// All current icon groups
static std::unordered_map<std::wstring, TileGroup> sIconGroups;

// The LiteStep module class
LSModule gLSModule(TEXT(MODULE_NAME), TEXT(MODULE_AUTHOR), MakeVersion(MODULE_VERSION));

// The next window in the clipboard viewer chain
static HWND sNextClipboardViewer;

// Re-checks that Explorer's icons are still hidden (Explorer restarts, or re-shows them).
static const UINT_PTR kExplorerIconsTimer = 1;


/// <summary>
/// The list view Explorer draws its desktop icons in, or null. It lives in a
/// SHELLDLL_DefView under Progman, or under a WorkerW once the wallpaper has been
/// animated (slideshow, "show desktop").
/// </summary>
static HWND FindExplorerIconView() {
  HWND view = FindWindowExW(FindWindowW(L"Progman", nullptr), nullptr, L"SHELLDLL_DefView", nullptr);
  for (HWND worker = nullptr; view == nullptr
      && (worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr; ) {
    view = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
  }
  return view != nullptr ? FindWindowExW(view, nullptr, L"SysListView32", nullptr) : nullptr;
}


/// <summary>
/// True if a visible group stands in for Explorer's desktop icons.
/// </summary>
static bool AnyGroupReplacesExplorerIcons() {
  for (auto &group : sIconGroups) {
    if (group.second.ReplacesExplorerIcons() && group.second.GetWindow()->IsVisible()) {
      return true;
    }
  }
  return false;
}


/// <summary>
/// Hides or shows Explorer's own desktop icons to match the groups. Only the window is
/// hidden; Explorer's "show desktop icons" setting is left as the user has it.
/// </summary>
static void SyncExplorerIcons() {
  HWND icons = FindExplorerIconView();
  if (icons == nullptr) {
    return;
  }
  bool hide = AnyGroupReplacesExplorerIcons();
  if (hide == (IsWindowVisible(icons) != FALSE)) {
    ShowWindowAsync(icons, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
  }
}


/// <summary>
/// !DesktopIcons [show|hide|toggle] -- the groups that replace Explorer's icons.
/// </summary>
static void DesktopIconsBang(HWND, LPCWSTR args) {
  WCHAR action[32] = L"";
  LiteStep::GetToken(args, action, nullptr, FALSE);
  bool show = _wcsicmp(action, L"show") == 0 ? true
    : _wcsicmp(action, L"hide") == 0 ? false
    : !AnyGroupReplacesExplorerIcons();

  for (auto &group : sIconGroups) {
    if (group.second.ReplacesExplorerIcons()) {
      if (show) {
        group.second.GetWindow()->Show();
      } else {
        group.second.GetWindow()->Hide();
      }
    }
  }
  SyncExplorerIcons();
}

/// <summary>
/// Creates a new group with the specified name.
/// </summary>
/// <param name="groupName">The name of the group to create.</param>
void CreateGroup(LPCWSTR groupName) {
  if (sIconGroups.find(groupName) == sIconGroups.end()) {
    sIconGroups.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(groupName),
      std::forward_as_tuple(groupName)
      );
  } else {
    ErrorHandler::Error(ErrorHandler::Level::Critical,
      L"Attempt to (re)create the already existing group %s!", groupName);
  }
}

/// <summary>
/// Reads through the .rc files and creates labels.
/// </summary>
static void LoadSettings() {
  LiteStep::IterateOverLineTokens(L"*nIcon", CreateGroup);
}

/// <summary>
/// Unloads everything created by .rc settings or at runtime.
/// </summary>
static void Unload() {
  sIconGroups.clear();
}

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

  sNextClipboardViewer = SetClipboardViewer(gLSModule.GetMessageWindow());
  OleInitialize(nullptr);
  LoadSettings();

  LiteStep::AddBangCommand(L"!DesktopIcons", DesktopIconsBang);
  SyncExplorerIcons();
  SetTimer(gLSModule.GetMessageWindow(), kExplorerIconsTimer, 2000, nullptr);
  return 0;
}

/// <summary>
/// Called by the LiteStep core when this module is about to be unloaded.
/// </summary>
EXPORT_CDECL(void) quitModule(HINSTANCE /* instance */) {
  LiteStep::RemoveBangCommand(L"!DesktopIcons");
  KillTimer(gLSModule.GetMessageWindow(), kExplorerIconsTimer);
  Unload();
  // No groups left: this gives Explorer its icons back.
  SyncExplorerIcons();
  gLSModule.DeInitalize();
  OleUninitialize();
}

/// <summary>
/// Handles the main window's messages.
/// </summary>
/// <param name="window">The window the message is for.</param>
/// <param name="message">The type of message.</param>
/// <param name="wParam">wParam</param>
/// <param name="lParam">lParam</param>
LRESULT WINAPI LSMessageHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
    SendMessage(LiteStep::GetLitestepWnd(), LM_REGISTERMESSAGE, (WPARAM)window,
      (LPARAM)gLSMessages);
    return 0;

  case WM_DESTROY:
    SendMessage(LiteStep::GetLitestepWnd(), LM_UNREGISTERMESSAGE, (WPARAM)window,
      (LPARAM)gLSMessages);
    ChangeClipboardChain(window, sNextClipboardViewer);
    return 0;

  case WM_DRAWCLIPBOARD:
    for (auto &item : sIconGroups) {
      item.second.HandleClipboardChange();
    }
    SendMessage(sNextClipboardViewer, message, wParam, lParam);
    return 0;

  case WM_CHANGECBCHAIN:
    if (HWND(wParam) == sNextClipboardViewer) {
      sNextClipboardViewer = (HWND)lParam;
    } else if (sNextClipboardViewer != nullptr) {
      SendMessage(sNextClipboardViewer, message, wParam, lParam);
    }
    return 0;

  case LM_FULLSCREENACTIVATED:
    for (auto &item : sIconGroups) {
      item.second.GetWindow()->FullscreenActivated((HMONITOR)wParam, (HWND)lParam);
    }
    return 0;

  case LM_FULLSCREENDEACTIVATED:
    for (auto &item : sIconGroups) {
      item.second.GetWindow()->FullscreenDeactivated((HMONITOR)wParam);
    }
    return 0;

  case WM_TIMER:
    if (wParam == kExplorerIconsTimer) {
      SyncExplorerIcons();
    }
    return 0;

  case LM_REFRESH:
    Unload();
    LoadSettings();
    SyncExplorerIcons();
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}


