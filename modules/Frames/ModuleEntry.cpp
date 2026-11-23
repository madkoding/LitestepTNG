//-------------------------------------------------------------------------------------------------
// /Frames/ModuleEntry.cpp
// The nModules Project
//
// nFrames entry points. Covers the native title bar of other programs' windows with a
// skinned one (settings prefix Frame, parts FrameTitle / FrameMin / FrameMax /
// FrameClose, each with an "Active" look for the foreground window).
//
// Only windows with a standard title bar get one; programs that draw their own are
// skipped (see TitleBar::GetNativeBarRect). Windows are followed with out-of-context
// WinEvent hooks, so the bar trails a dragged window by a moment.
//
// RC:    *FramesExclude <window class>   never cover windows of this class
// Bangs: !FramesToggle                   removes / restores every bar
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include "TitleBar.hpp"
#include "Version.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// The LSModule class.
LSModule gLSModule(TEXT(MODULE_NAME), TEXT(MODULE_AUTHOR), MakeVersion(MODULE_VERSION));

// The messages we want from the core.
static UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, 0 };

static std::map<HWND, std::unique_ptr<TitleBar>> sBars;
static std::vector<std::wstring> sExcludedClasses;
static std::vector<HWINEVENTHOOK> sHooks;
static bool sEnabled = true;

// Activating a window raises it above its bar, and the notification can arrive before
// Windows has finished reordering. So after such an event the stacking is checked a few
// more times, and a slow watchdog catches whatever no event reported.
static const UINT_PTR kSettleTimer = 1;
static const UINT_PTR kWatchdogTimer = 2;
static int sSettlePasses = 0;


static void RestackAll() {
  for (auto &bar : sBars) {
    bar.second->Restack();
  }
}


static void ScheduleSettle() {
  sSettlePasses = 4;
  SetTimer(gLSModule.GetMessageWindow(), kSettleTimer, 40, nullptr);
}


/// <summary>
/// The integrity level of a process (medium for ordinary programs, high for ones run
/// as administrator), or -1 if it cannot even be read.
/// </summary>
static long IntegrityLevel(HANDLE process) {
  long level = -1;
  HANDLE token = nullptr;
  if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
    BYTE buffer[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    DWORD size = 0;
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer), &size)) {
      PSID sid = ((TOKEN_MANDATORY_LABEL *)buffer)->Label.Sid;
      level = (long)*GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    }
    CloseHandle(token);
  }
  return level;
}


/// <summary>
/// Windows does not let a program move, or send commands to, the windows of a program
/// running at a higher integrity level (anything started as administrator). A frame
/// there would be a dead decoration over a working title bar, so those windows keep
/// their own. Posting a test message does not detect this: it "succeeds" and is dropped.
/// </summary>
static bool CanControl(DWORD processId) {
  static const long ownLevel = IntegrityLevel(GetCurrentProcess());
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (process == nullptr) {
    return false;
  }
  long level = IntegrityLevel(process);
  CloseHandle(process);
  return level >= 0 && level <= ownLevel;
}


static bool IsExcluded(HWND window) {
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (processId == GetCurrentProcessId() || !CanControl(processId)) {
    return true;
  }

  WCHAR className[256] = L"";
  GetClassNameW(window, className, _countof(className));
  for (const std::wstring &excluded : sExcludedClasses) {
    if (_wcsicmp(excluded.c_str(), className) == 0) {
      return true;
    }
  }
  return false;
}


/// <summary>
/// Brings one window's bar up to date: creates it, moves it, or removes it.
/// </summary>
static void SyncWindow(HWND window) {
  auto existing = sBars.find(window);
  if (existing != sBars.end()) {
    if (!sEnabled || !IsWindow(window) || !existing->second->Sync()) {
      sBars.erase(existing);
    }
    return;
  }

  RECT bar;
  if (sEnabled && TitleBar::GetNativeBarRect(window, bar) && !IsExcluded(window)) {
    auto created = sBars.emplace(window, std::unique_ptr<TitleBar>(new TitleBar(window))).first;
    if (!created->second->Sync()) {
      sBars.erase(created);
    }
  }
}


static void SyncAll() {
  std::vector<HWND> windows;
  for (auto &bar : sBars) {
    windows.push_back(bar.first);
  }
  EnumWindows([] (HWND window, LPARAM lParam) -> BOOL {
    ((std::vector<HWND> *)lParam)->push_back(window);
    return TRUE;
  }, (LPARAM)&windows);
  for (HWND window : windows) {
    SyncWindow(window);
  }
}


static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND window, LONG object,
    LONG child, DWORD, DWORD) {
  // A change in the order of top-level windows is reported for the desktop's client
  // area, not for the window that moved, so it is handled before the filter below.
  if (event == EVENT_OBJECT_REORDER) {
    RestackAll();
    ScheduleSettle();
    return;
  }

  // Only whole windows: this also drops the caret, the cursor and every control.
  if (object != OBJID_WINDOW || child != CHILDID_SELF || window == nullptr) {
    return;
  }

  switch (event) {
  case EVENT_SYSTEM_FOREGROUND:
    // The active window concerns every bar: one gains the Active look, one loses it.
    SyncWindow(window);
    {
      std::vector<HWND> windows;
      for (auto &bar : sBars) {
        windows.push_back(bar.first);
      }
      for (HWND tracked : windows) {
        SyncWindow(tracked);
      }
    }
    ScheduleSettle();
    return;

  case EVENT_OBJECT_DESTROY:
    sBars.erase(window);
    return;
  }

  if (sBars.find(window) != sBars.end() || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) == 0) {
    SyncWindow(window);
  }
}


static void Hook(DWORD first, DWORD last) {
  HWINEVENTHOOK hook = SetWinEventHook(first, last, nullptr, WinEventProc, 0, 0,
    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  if (hook != nullptr) {
    sHooks.push_back(hook);
  }
}


static void LoadSettings() {
  sExcludedClasses.clear();
  LiteStep::IterateOverLineTokens(L"*FramesExclude", [] (LPCWSTR className) {
    sExcludedClasses.push_back(className);
  });
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

  LoadSettings();

  // Narrow ranges: one hook over everything would deliver every focus and selection
  // change of every program to the shell's thread.
  Hook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND);
  Hook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND);
  Hook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER);            // destroy, show, hide, reorder
  Hook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_NAMECHANGE);  // move/size, title
  Hook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED);

  LiteStep::AddBangCommand(L"!FramesToggle", [] (HWND, LPCWSTR) {
    sEnabled = !sEnabled;
    SyncAll();
  });

  SyncAll();
  SetTimer(gLSModule.GetMessageWindow(), kWatchdogTimer, 500, nullptr);
  return 0;
}


/// <summary>
/// Handles the main window's messages.
/// </summary>
LRESULT WINAPI LSMessageHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  using LiteStep::GetLitestepWnd;

  switch (message) {
  case WM_CREATE:
    SendMessage(GetLitestepWnd(), LM_REGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
    return 0;

  case WM_DESTROY:
    SendMessage(GetLitestepWnd(), LM_UNREGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
    return 0;

  case WM_TIMER:
    if (wParam == kSettleTimer) {
      RestackAll();
      if (--sSettlePasses <= 0) {
        KillTimer(window, kSettleTimer);
      }
    } else if (wParam == kWatchdogTimer) {
      // A full check, not only the stacking: some changes come without any event for
      // the window (the work area being corrected under a maximized window, a caption
      // that changes height), and the bar would stay where it was.
      std::vector<HWND> windows;
      for (auto &bar : sBars) {
        windows.push_back(bar.first);
      }
      for (HWND tracked : windows) {
        SyncWindow(tracked);
      }
    }
    return 0;

  case WM_DISPLAYCHANGE:
  case WM_SETTINGCHANGE:
    SyncAll();
    return 0;

  case LM_REFRESH:
    // The skin may have changed: build the bars again.
    sBars.clear();
    LoadSettings();
    SyncAll();
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}


/// <summary>
/// Called by the LiteStep core when this module is about to be unloaded.
/// </summary>
EXPORT_CDECL(void) quitModule(HINSTANCE /* instance */) {
  LiteStep::RemoveBangCommand(L"!FramesToggle");
  KillTimer(gLSModule.GetMessageWindow(), kSettleTimer);
  KillTimer(gLSModule.GetMessageWindow(), kWatchdogTimer);
  for (HWINEVENTHOOK hook : sHooks) {
    UnhookWinEvent(hook);
  }
  sHooks.clear();
  sBars.clear();
  gLSModule.DeInitalize();
}
