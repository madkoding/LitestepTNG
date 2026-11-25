//-------------------------------------------------------------------------------------------------
// /VWM/Transition.cpp
// The nModules Project
//
// The effect shown when switching desktops.
//
// Other programs' windows cannot be animated smoothly from outside: each one would have
// to repaint at every step. So nothing real is animated. A picture of each monitor's
// work area is taken and shown on top, the desktops are switched underneath, and the
// picture then slides a short way and fades out. Only the work area is covered, so the
// theme's bars, which do not change, stay still.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"

#include "Transition.h"
#include "../ModuleKit/FrameClock.hpp"
#include "../Utilities/StopWatch.hpp"

#include <vector>

static const WCHAR kClassName[] = L"LSVWMTransition";

// Time the picture stays untouched first, while the new desktop's windows arrive and
// paint behind it.
static const DWORD kHold = 60;

struct Snapshot {
  HWND window;
  HDC dc;
  HBITMAP bitmap;
  HGDIOBJ previous;
  RECT area;
};

static std::vector<Snapshot> sSnapshots;
static bool sEnabled = false;
static DWORD sDuration = 200;
static int sDistance = 120;
static int sDirection = 0;
// High-resolution: GetTickCount64 moves in ~15.6 ms steps, coarser than a frame.
static StopWatch sClock;
static HINSTANCE sInstance = nullptr;


static void Clear() {
  for (Snapshot &snapshot : sSnapshots) {
    DestroyWindow(snapshot.window);
    SelectObject(snapshot.dc, snapshot.previous);
    DeleteObject(snapshot.bitmap);
    DeleteDC(snapshot.dc);
  }
  sSnapshots.clear();
}


static BOOL CALLBACK CaptureMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM) {
  MONITORINFO info = { sizeof(info) };
  if (!GetMonitorInfoW(monitor, &info) || IsRectEmpty(&info.rcWork)) {
    return TRUE;
  }
  const RECT &area = info.rcWork;
  SIZE size = { area.right - area.left, area.bottom - area.top };

  Snapshot snapshot = {};
  snapshot.area = area;

  HDC screen = GetDC(nullptr);
  snapshot.dc = CreateCompatibleDC(screen);
  snapshot.bitmap = CreateCompatibleBitmap(screen, size.cx, size.cy);
  snapshot.previous = SelectObject(snapshot.dc, snapshot.bitmap);
  BitBlt(snapshot.dc, 0, 0, size.cx, size.cy, screen, area.left, area.top, SRCCOPY);

  // Topmost and click-through: for a fifth of a second the user's clicks go to the real
  // windows underneath, not to a picture of the old ones.
  snapshot.window = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    kClassName, L"", WS_POPUP, area.left, area.top, size.cx, size.cy, nullptr, nullptr, sInstance, nullptr);
  if (snapshot.window != nullptr) {
    POINT position = { area.left, area.top }, origin = { 0, 0 };
    // No AC_SRC_ALPHA: a screen capture has no meaningful alpha channel, the whole
    // picture is opaque and faded as one.
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
    UpdateLayeredWindow(snapshot.window, screen, &position, &size, snapshot.dc, &origin, 0, &blend, ULW_ALPHA);
    ShowWindow(snapshot.window, SW_SHOWNOACTIVATE);
    sSnapshots.push_back(snapshot);
  } else {
    SelectObject(snapshot.dc, snapshot.previous);
    DeleteObject(snapshot.bitmap);
    DeleteDC(snapshot.dc);
  }
  ReleaseDC(nullptr, screen);
  return TRUE;
}


void Transition::Load(HINSTANCE instance) {
  sInstance = instance;
  sEnabled = LiteStep::GetRCBool(L"VWMTransition", TRUE) != FALSE;
  sDuration = (DWORD)LiteStep::GetRCInt(L"VWMTransitionDuration", 200);
  sDistance = LiteStep::GetRCInt(L"VWMTransitionDistance", 120);
  if (sDuration < 20) sDuration = 20;
  if (sDuration > 2000) sDuration = 2000;

  WNDCLASSEXW windowClass = { sizeof(windowClass) };
  windowClass.lpfnWndProc = DefWindowProcW;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = kClassName;
  RegisterClassExW(&windowClass);
}


void Transition::Unload(HINSTANCE instance) {
  Clear();
  UnregisterClassW(kClassName, instance);
}


void Transition::Begin(HWND timerWindow, int direction) {
  // A switch in the middle of another: drop the old picture and start over from what
  // is on screen now.
  Clear();
  if (!sEnabled) {
    return;
  }
  sDirection = direction;
  EnumDisplayMonitors(nullptr, nullptr, CaptureMonitor, 0);
  if (!sSnapshots.empty()) {
    sClock.Clock();
    FrameClock::Subscribe(timerWindow);
  }
}


void Transition::OnFrame(HWND timerWindow) {
  FrameClock::Ack(timerWindow);

  float elapsed = sClock.GetTime() * 1000.0f;
  if (sSnapshots.empty() || elapsed >= float(kHold + sDuration)) {
    FrameClock::Unsubscribe(timerWindow);
    Clear();
    return;
  }
  if (elapsed < float(kHold)) {
    return;
  }

  // Ease out: quick at first, settling at the end.
  float progress = float(elapsed - kHold) / float(sDuration);
  float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
  int offset = int(float(sDirection * sDistance) * eased);
  BLENDFUNCTION blend = { AC_SRC_OVER, 0, BYTE(255.0f * (1.0f - progress)), 0 };

  for (Snapshot &snapshot : sSnapshots) {
    const RECT &area = snapshot.area;
    // The window moves; a region keeps it inside its own monitor's work area, so it
    // never slides over the bars or onto the next monitor.
    HRGN visible = offset >= 0
      ? CreateRectRgn(0, 0, (area.right - area.left) - offset, area.bottom - area.top)
      : CreateRectRgn(-offset, 0, area.right - area.left, area.bottom - area.top);
    SetWindowRgn(snapshot.window, visible, FALSE);

    POINT position = { area.left + offset, area.top };
    UpdateLayeredWindow(snapshot.window, nullptr, &position, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
  }
}
