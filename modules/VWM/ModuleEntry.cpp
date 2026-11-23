//-------------------------------------------------------------------------------------------------
// /VWM/ModuleEntry.cpp
// The nModules Project
//
// nVWM entry points and a minimal virtual window manager.
//
// Windows' own virtual desktops live in explorer.exe, which is not running when LiteStep is
// the shell, so this is the classic LiteStep approach: every desktop but the current one is
// "parked" by shifting its windows far off to the right of the virtual screen. Nothing is
// hidden, so applications keep running normally and a window can always be brought back.
//
// The desktop a parked window belongs to is stored on the window itself (a window property),
// not in this module. That way a recycle or a crash cannot strand windows: whoever loads
// next can find every parked window and bring it back.
//
// Bangs:   !VWMDesk <1..N>   !VWMNext   !VWMPrev   !VWMGather
//          !VWMSend <1..N> [window handle]   sends the active window (or the given one,
//                                            decimal or 0x hex) to desktop N; you stay here
// RC:      VWMDesktops <N>             number of desktops (default 4)
//          VWMDesk<N>Command <bang>    run after switching to desktop N (e.g. to move
//                                      a "current desktop" marker in the theme)
//          VWMTransition <bool>        slide-and-fade effect when switching (default true)
//          VWMTransitionDuration <ms>  default 200
//          VWMTransitionDistance <px>  how far the old desktop slides, default 120
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"
#include "../Utilities/WindowActivation.h"

#include "Transition.h"
#include "Version.h"

#include <dwmapi.h>
#include <strsafe.h>

#pragma comment(lib, "dwmapi.lib")

// The LSModule class
LSModule gLSModule(TEXT(MODULE_NAME), TEXT(MODULE_AUTHOR), MakeVersion(MODULE_VERSION));

// The messages we want from the core
const UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, LM_WINDOWACTIVATED, 0 };

// Window property holding (desktop index + 1) for a parked window. Absent = on screen.
static const WCHAR kDeskProp[] = L"LSVWMDesk";

// How far parked windows are shifted. Must exceed the width of the whole virtual screen
// and keep coordinates inside the 16-bit range some window messages still use.
static const int kParkOffset = 20000;

static int sDeskCount = 4;
static int sCurrentDesk = 0;


/// <summary>
/// Returns the desktop a window is parked on, or -1 if it is not parked.
/// </summary>
static int ParkedDesk(HWND window) {
  return int(INT_PTR(GetPropW(window, kDeskProp))) - 1;
}


/// <summary>
/// True for windows that belong to a desktop: visible, not minimized application windows.
/// Shell windows (ours), tool windows and cloaked windows stay where they are.
/// </summary>
static bool IsManageable(HWND window) {
  if (!IsWindowVisible(window) || IsIconic(window)) {
    return false;
  }

  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process == GetCurrentProcessId()) {
    return false;
  }

  LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
  if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW)) {
    return false;
  }

  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
    return false;
  }

  RECT rect;
  if (!GetWindowRect(window, &rect) || IsRectEmpty(&rect)) {
    return false;
  }

  WCHAR className[64] = L"";
  GetClassNameW(window, className, _countof(className));
  if (_wcsicmp(className, L"Progman") == 0 || _wcsicmp(className, L"WorkerW") == 0 ||
      _wcsicmp(className, L"Shell_TrayWnd") == 0 || _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0) {
    return false;
  }

  return true;
}


// Window property holding where a parked window came from (its left edge, plus a bias so
// that a stored zero is not mistaken for "no property").
static const WCHAR kHomeProp[] = L"LSVWMHomeX";
static const INT_PTR kHomeBias = 0x40000000;


/// <summary>
/// Moves a window horizontally without touching its size, z-order or activation.
/// Asynchronous, so a hung application cannot hang the shell.
/// </summary>
static void MoveTo(HWND window, int x, int y) {
  SetWindowPos(window, nullptr, x, y, 0, 0,
    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}


/// <summary>
/// Parks a window on a desktop. Returns false if the window cannot be tagged (it runs
/// elevated); it is then left alone rather than lost track of.
/// </summary>
static bool Park(HWND window, int desk) {
  RECT rect;
  if (!GetWindowRect(window, &rect) || !SetPropW(window, kDeskProp, HANDLE(INT_PTR(desk + 1)))) {
    return false;
  }
  // The moves are asynchronous: a slow program may not have applied this one when the
  // next switch comes. So the way back is not "current position minus the offset",
  // which would then be computed from a stale position and leave the window stranded
  // off screen, but the position remembered here.
  SetPropW(window, kHomeProp, HANDLE(INT_PTR(rect.left) + kHomeBias));
  MoveTo(window, rect.left + kParkOffset, rect.top);
  return true;
}


/// <summary>
/// Brings a parked window back to where it was.
/// </summary>
static void Unpark(HWND window) {
  RECT rect;
  if (!GetWindowRect(window, &rect)) {
    return;
  }
  INT_PTR home = INT_PTR(GetPropW(window, kHomeProp));
  RemovePropW(window, kDeskProp);
  RemovePropW(window, kHomeProp);
  MoveTo(window, home != 0 ? int(home - kHomeBias) : rect.left - kParkOffset, rect.top);
}


struct SwitchContext {
  int from;
  int to;
  HWND firstRestored; // topmost window of the new desktop, EnumWindows goes top to bottom
};


static BOOL CALLBACK SwitchProc(HWND window, LPARAM lParam) {
  SwitchContext *context = (SwitchContext*)lParam;
  int desk = ParkedDesk(window);

  if (desk == context->to) {
    // Belongs to the desktop we are entering: bring it back.
    Unpark(window);
    if (context->firstRestored == nullptr) {
      context->firstRestored = window;
    }
  } else if (desk < 0 && IsManageable(window)) {
    // On screen, so it belongs to the desktop we are leaving: park it. If we cannot tag
    // the window (e.g. it runs elevated) we leave it alone rather than lose track of it.
    Park(window, context->from);
  }
  return TRUE;
}


/// <summary>
/// Switches to the given desktop.
/// </summary>
/// <param name="desk">Zero-based desktop index.</param>
/// <param name="activate">Give focus to the top window of the new desktop. False when the
/// switch was caused by a window being activated, which already has the focus.</param>
static void SwitchTo(int desk, bool activate) {
  if (desk < 0 || desk >= sDeskCount || desk == sCurrentDesk) {
    return;
  }

  // Freeze what is on screen; the picture leaves towards the side the old desktop is on.
  Transition::Begin(gLSModule.GetMessageWindow(), desk > sCurrentDesk ? -1 : 1);

  SwitchContext context = { sCurrentDesk, desk, nullptr };
  EnumWindows(SwitchProc, (LPARAM)&context);
  sCurrentDesk = desk;

  if (activate) {
    // Otherwise the keyboard would keep going to a window that is now off screen.
    SetForegroundWindow(context.firstRestored ? context.firstRestored : LiteStep::GetLitestepWnd());
  }

  WCHAR key[32], command[MAX_LINE_LENGTH];
  StringCchPrintfW(key, _countof(key), L"VWMDesk%dCommand", desk + 1);
  if (LiteStep::GetRCLine(key, command, _countof(command), L"") && command[0] != L'\0') {
    LiteStep::LSExecute(nullptr, command, SW_SHOWNORMAL);
  }
}


static BOOL CALLBACK GatherProc(HWND window, LPARAM) {
  if (ParkedDesk(window) >= 0) {
    Unpark(window);
    return TRUE;
  }

  // Rescue: an untagged window sitting a whole number of park offsets away from the
  // screen was stranded there (by a version that moved windows relative to a stale
  // position, or by a crash between tagging and moving). Bring it back.
  RECT rect;
  if (IsManageable(window) && GetWindowRect(window, &rect)) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    if (rect.right < left - kParkOffset / 4 || rect.left > right + kParkOffset / 4) {
      int offsets = (rect.left + (rect.left < 0 ? -kParkOffset / 2 : kParkOffset / 2)) / kParkOffset;
      if (offsets != 0) {
        MoveTo(window, rect.left - offsets * kParkOffset, rect.top);
      }
    }
  }
  return TRUE;
}


/// <summary>
/// Brings every parked window back to the screen.
/// </summary>
static void Gather() {
  EnumWindows(GatherProc, 0);
}


/// <summary>
/// Sends a window that is on screen to another desktop, without following it.
/// </summary>
/// <param name="desk">Zero-based desktop index.</param>
static void SendTo(HWND window, int desk) {
  if (desk < 0 || desk >= sDeskCount || desk == sCurrentDesk || window == nullptr
      || !IsManageable(window) || ParkedDesk(window) >= 0) {
    return;
  }
  if (!Park(window, desk)) {
    return;
  }

  // The keyboard would otherwise keep going to the window that just left. The next
  // window down takes over (EnumWindows goes top to bottom).
  struct Next { HWND sent; HWND found; } next = { window, nullptr };
  EnumWindows([] (HWND candidate, LPARAM lParam) -> BOOL {
    Next *next = (Next *)lParam;
    if (candidate != next->sent && ParkedDesk(candidate) < 0 && IsManageable(candidate)) {
      next->found = candidate;
      return FALSE;
    }
    return TRUE;
  }, (LPARAM)&next);
  WindowActivation::ForceForeground(next.found != nullptr ? next.found : LiteStep::GetLitestepWnd());
}


static void SendBang(HWND, LPCWSTR args) {
  WCHAR deskToken[MAX_LINE_LENGTH] = L"", windowToken[MAX_LINE_LENGTH] = L"";
  LPWSTR tokens[] = { deskToken, windowToken };
  LiteStep::CommandTokenize(args, tokens, _countof(tokens), nullptr);

  HWND window = windowToken[0] != L'\0'
    ? (HWND)(UINT_PTR)_wcstoui64(windowToken, nullptr, 0)
    : GetForegroundWindow();
  SendTo(window, _wtoi(deskToken) - 1);
}


static void DeskBang(HWND, LPCWSTR args) {
  SwitchTo(_wtoi(args) - 1, true);
}

static void NextBang(HWND, LPCWSTR) {
  SwitchTo((sCurrentDesk + 1) % sDeskCount, true);
}

static void PrevBang(HWND, LPCWSTR) {
  SwitchTo((sCurrentDesk + sDeskCount - 1) % sDeskCount, true);
}

static void GatherBang(HWND, LPCWSTR) {
  Gather();
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

  sDeskCount = LiteStep::GetRCInt(L"VWMDesktops", 4);
  if (sDeskCount < 1) sDeskCount = 1;
  if (sDeskCount > 16) sDeskCount = 16;
  sCurrentDesk = 0;

  Transition::Load(instance);

  // Start from a clean state: anything left parked by a previous instance comes back.
  Gather();

  LiteStep::AddBangCommand(L"!VWMDesk", DeskBang);
  LiteStep::AddBangCommand(L"!VWMNext", NextBang);
  LiteStep::AddBangCommand(L"!VWMPrev", PrevBang);
  LiteStep::AddBangCommand(L"!VWMGather", GatherBang);
  LiteStep::AddBangCommand(L"!VWMSend", SendBang);

  return 0;
}


/// <summary>
/// Called by the LiteStep core when this module is about to be unloaded.
/// </summary>
EXPORT_CDECL(void) quitModule(HINSTANCE /* instance */) {
  LiteStep::RemoveBangCommand(L"!VWMDesk");
  LiteStep::RemoveBangCommand(L"!VWMNext");
  LiteStep::RemoveBangCommand(L"!VWMPrev");
  LiteStep::RemoveBangCommand(L"!VWMGather");
  LiteStep::RemoveBangCommand(L"!VWMSend");

  // Never leave windows off screen with nothing loaded that could bring them back.
  Gather();
  Transition::Unload(gLSModule.GetInstance());

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
  switch(message) {
  case WM_CREATE:
    SendMessage(GetLitestepWnd(), LM_REGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
    return 0;

  case WM_DESTROY:
    SendMessage(GetLitestepWnd(), LM_UNREGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
    return 0;

  case LM_WINDOWACTIVATED:
    {
      // A parked window got the focus (taskbar click, Alt+Tab, the app itself): follow it.
      int desk = ParkedDesk((HWND)wParam);
      if (desk >= 0) {
        SwitchTo(desk, false);
      }
    }
    return 0;

  case WM_TIMER:
    if (Transition::OnTimer(window, wParam)) {
      return 0;
    }
    break;

  case LM_REFRESH:
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}
