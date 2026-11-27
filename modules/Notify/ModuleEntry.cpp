//-------------------------------------------------------------------------------------------------
// /Notify/ModuleEntry.cpp
// The nModules Project
//
// nNotify entry points. Shows the Windows notifications as a column of boxes on the right
// side of the desktop, stacked from the top down in the order they arrived.
//
// RC settings (the boxes themselves are skinned through NotifyBox*, NotifyBoxTitle*,
// NotifyBoxText*, NotifyBoxTime* and NotifyBoxClose*):
//   NotifyMarginTop / NotifyMarginRight / NotifyMarginBottom  px from the work area's edges
//   NotifySpacing              px between two boxes
//   NotifyTimeFormat           strftime pattern for the time line
//   NotifyPollInterval         ms between two reads of the Windows notification list
//   NotifyShowExisting         also show what is already in the notification centre on load
//   NotifyDismissFromSystem    the X also removes the notification from Windows
//   NotifyClickDismisses       clicking a box (which goes to its app) also closes it
//   NotifySound                .wav played when a notification arrives; none by default
//   NotifyOverflow             when the column is full: DropOldest (default) removes the
//                              oldest box from the desktop, Wait keeps new ones hidden
//                              until there is room
//
// Bangs: !NotifyTest ["app" ["title" ["text"]]], !NotifyClear [test]
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include "AppActivation.hpp"
#include "Listener.hpp"
#include "NotificationBox.hpp"
#include "Version.h"

#include <memory>
#include <vector>

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// LiteStep's own LM_ messages start at WM_APP; these sit well above the ones it defines.
#define WM_NOTIFY_UPDATE (WM_APP + 0x3A01)
// A closing box finished its exit animation: wParam is the box.
#define WM_NOTIFY_CLOSED (WM_APP + 0x3A04)

// Ids for !NotifyTest boxes; Windows counts its own ids up from a small number.
#define TEST_ID_FLAG 0x80000000u

// The LSModule class.
LSModule gLSModule(TEXT(MODULE_NAME), TEXT(MODULE_AUTHOR), MakeVersion(MODULE_VERSION));

// The messages we want from the core.
static UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, LM_FULLSCREENACTIVATED,
    LM_FULLSCREENDEACTIVATED, 0 };

// The boxes, oldest first; that is also their order on screen, top to bottom.
static std::vector<std::unique_ptr<NotificationBox>> sBoxes;
// Boxes playing their exit animation; deleted when it ends.
static std::vector<std::unique_ptr<NotificationBox>> sClosing;

static int sMarginTop, sMarginRight, sMarginBottom, sSpacing, sMoveDuration;
static WCHAR sTimeFormat[64];
static bool sDismissFromSystem, sClickDismisses, sOverflowDropsOldest;
static WCHAR sSound[MAX_PATH];
static UINT32 sNextTestId = TEST_ID_FLAG;


/// <summary>
/// Takes a box out of the column and lets it leave with its exit animation
/// (NotifyBoxHideAnimation*); it is deleted when the animation ends.
/// </summary>
static void CloseBox(std::vector<std::unique_ptr<NotificationBox>>::iterator box) {
  NotificationBox *raw = box->get();
  sClosing.push_back(std::move(*box));
  sBoxes.erase(box);
  raw->GetWindow()->HideAnimated([raw] () {
    // Not from inside the box's own window procedure: delete it from the module's queue.
    PostMessage(gLSModule.GetMessageWindow(), WM_NOTIFY_CLOSED, (WPARAM)raw, 0);
  });
}


/// <summary>
/// Stacks the boxes down the right edge of the primary monitor's work area.
/// </summary>
static void Relayout() {
  RECT area;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  const float right = float(area.right - sMarginRight);
  const float bottom = float(area.bottom - sMarginBottom);

  for (;;) {
    float y = float(area.top + sMarginTop);
    size_t index = 0;
    for (; index < sBoxes.size(); ++index) {
      NotificationBox &box = *sBoxes[index];
      // The first box is always shown, however small the column is.
      if (index > 0 && y + box.GetHeight() > bottom) {
        break;
      }
      box.Place(right - box.GetWidth(), y, sMoveDuration);
      y += box.GetHeight() + float(sSpacing);
    }

    if (index == sBoxes.size()) {
      return;
    }

    // The column is full (index is the first box that does not fit).
    if (!sOverflowDropsOldest) {
      // Keep them all: the ones that do not fit wait, hidden, until closing others
      // makes room.
      for (; index < sBoxes.size(); ++index) {
        sBoxes[index]->Hide();
      }
      return;
    }
    // Drop the oldest box (it stays in the Windows notification centre) and lay the
    // column out again, so the newest is always on screen.
    CloseBox(sBoxes.begin());
  }
}


static void AddBox(const ToastData &data) {
  sBoxes.emplace_back(new NotificationBox(data, sTimeFormat));
}


/// <summary>
/// Plays the theme's notification sound. Windows plays none of its own while "do not
/// disturb" keeps its banners away.
/// </summary>
static void PlayNotifySound() {
  if (sSound[0] != L'\0') {
    PlaySoundW(sSound, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  }
}


/// <summary>
/// Removes a box.
/// </summary>
/// <param name="dismiss">True when the user closed it, false when Windows dropped it.</param>
static void RemoveBox(UINT32 id, bool dismiss) {
  for (auto box = sBoxes.begin(); box != sBoxes.end(); ++box) {
    if ((*box)->GetData().id == id) {
      CloseBox(box);
      if (dismiss && sDismissFromSystem && (id & TEST_ID_FLAG) == 0) {
        Listener::Dismiss(id);
      }
      return;
    }
  }
}


static void LoadSettings() {
  Settings settings(L"Notify");
  sMarginTop = settings.GetInt(L"MarginTop", 8);
  sMarginRight = settings.GetInt(L"MarginRight", 8);
  sMarginBottom = settings.GetInt(L"MarginBottom", 8);
  sSpacing = settings.GetInt(L"Spacing", 6);
  // How long the others take to close up when a box leaves (ms; 0 = jump).
  sMoveDuration = settings.GetInt(L"MoveDuration", 220);
  sDismissFromSystem = settings.GetBool(L"DismissFromSystem", true);
  sClickDismisses = settings.GetBool(L"ClickDismisses", true);
  WCHAR overflow[32];
  settings.GetString(L"Overflow", overflow, _countof(overflow), L"DropOldest");
  sOverflowDropsOldest = _wcsicmp(overflow, L"Wait") != 0;
  settings.GetString(L"Sound", sSound, _countof(sSound), L"");
  settings.GetString(L"TimeFormat", sTimeFormat, _countof(sTimeFormat), L"%d . %m . %Y   %H:%M");

  Listener::Start(gLSModule.GetMessageWindow(), WM_NOTIFY_UPDATE,
    (UINT)settings.GetInt(L"PollInterval", 1000), settings.GetBool(L"ShowExisting", false));
}


/// <summary>
/// !NotifyTest ["app" ["title" ["text"]]] -- shows a box without a real notification.
/// </summary>
static void TestBang(HWND, LPCWSTR args) {
  WCHAR app[MAX_LINE_LENGTH] = L"", title[MAX_LINE_LENGTH] = L"", body[MAX_LINE_LENGTH] = L"";
  LPWSTR tokens[] = { app, title, body };
  LiteStep::CommandTokenize(args, tokens, _countof(tokens), nullptr);

  ToastData data = { sNextTestId++ };
  data.app = *app ? app : L"LiteStep";
  data.title = *title ? title : L"Test notification";
  data.body = body;
  GetSystemTimeAsFileTime(&data.time);
  sNextTestId |= TEST_ID_FLAG;

  AddBox(data);
  Relayout();
  PlayNotifySound();
}


/// <summary>
/// !NotifyClear -- closes every box, as if each X had been clicked.
/// !NotifyClear test -- closes only the boxes made by !NotifyTest, leaving the real
/// notifications (which a full clear also removes from Windows) alone.
/// </summary>
static void ClearBang(HWND, LPCWSTR args) {
  WCHAR what[32] = L"";
  LiteStep::GetToken(args, what, nullptr, FALSE);
  bool onlyTests = _wcsicmp(what, L"test") == 0;

  std::vector<UINT32> ids;
  for (auto &box : sBoxes) {
    UINT32 id = box->GetData().id;
    if (!onlyTests || (id & TEST_ID_FLAG) != 0) {
      ids.push_back(id);
    }
  }
  for (UINT32 id : ids) {
    RemoveBox(id, true);
  }
  Relayout();
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

  LiteStep::AddBangCommand(L"!NotifyTest", TestBang);
  LiteStep::AddBangCommand(L"!NotifyClear", ClearBang);

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

  case WM_NOTIFY_UPDATE:
    {
      std::vector<ToastData> added;
      std::vector<UINT32> removed;
      Listener::Drain(added, removed);
      for (UINT32 id : removed) {
        RemoveBox(id, false);
      }
      for (const ToastData &data : added) {
        AddBox(data);
      }
      Relayout();
      // Once, however many arrived together.
      if (!added.empty()) {
        PlayNotifySound();
      }
    }
    return 0;

  case WM_NOTIFY_ACTIVATE:
    for (auto &box : sBoxes) {
      if (box->GetData().id == (UINT32)wParam) {
        AppActivation::Activate(box->GetData().appId.c_str());
        if (sClickDismisses) {
          RemoveBox((UINT32)wParam, true);
          Relayout();
        }
        break;
      }
    }
    return 0;

  case WM_NOTIFY_DISMISS:
    RemoveBox((UINT32)wParam, true);
    Relayout();
    return 0;

  case WM_NOTIFY_CLOSED:
    for (auto box = sClosing.begin(); box != sClosing.end(); ++box) {
      if ((WPARAM)box->get() == wParam) {
        sClosing.erase(box);
        break;
      }
    }
    return 0;

  case WM_DISPLAYCHANGE:
  case WM_SETTINGCHANGE:
    Relayout();
    return 0;

  case LM_FULLSCREENACTIVATED:
    for (auto &box : sBoxes) {
      box->GetWindow()->FullscreenActivated((HMONITOR)wParam, (HWND)lParam);
    }
    return 0;

  case LM_FULLSCREENDEACTIVATED:
    for (auto &box : sBoxes) {
      box->GetWindow()->FullscreenDeactivated((HMONITOR)wParam);
    }
    return 0;

  case LM_REFRESH:
    {
      // The skin may have changed: rebuild the boxes from what they were showing.
      std::vector<ToastData> shown;
      for (auto &box : sBoxes) {
        shown.push_back(box->GetData());
      }
      sBoxes.clear();
      sClosing.clear();
      LoadSettings();
      for (const ToastData &data : shown) {
        AddBox(data);
      }
      Relayout();
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}


/// <summary>
/// Called by the LiteStep core when this module is about to be unloaded.
/// </summary>
EXPORT_CDECL(void) quitModule(HINSTANCE /* instance */) {
  LiteStep::RemoveBangCommand(L"!NotifyTest");
  LiteStep::RemoveBangCommand(L"!NotifyClear");
  Listener::Stop();
  sBoxes.clear();
  sClosing.clear();
  gLSModule.DeInitalize();
}
