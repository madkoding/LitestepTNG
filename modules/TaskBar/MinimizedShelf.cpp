//-------------------------------------------------------------------------------------------------
// /TaskBar/MinimizedShelf.cpp
// The nModules Project
//
// See MinimizedShelf.hpp.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"
#include "../Utilities/WindowActivation.h"

#include "MinimizedShelf.hpp"
#include "Taskbar.hpp"
#include "WindowManager.h"

#include <algorithm>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

extern LSModule gLSModule;

std::unique_ptr<MinimizedShelf> gMinimizedShelf;

namespace {
  // nVWM marks the windows it has parked on another desktop.
  const WCHAR kParkedProp[] = L"LSVWMDesk";

  // How often the thumbnails check where their buttons are, while there are any (ms).
  const UINT kRepositionInterval = 120;

  const WindowSettings sThumbDefaults([] (WindowSettings &defaults) {
    // On the desktop, under the windows, like desktop icons.
    defaults.onDesktop = true;
    defaults.alwaysOnTop = false;
  });

  const StateRender<MinimizedShelf::States>::InitData sInitData(
      [] (StateRender<MinimizedShelf::States>::InitData &initData) {
    initData[MinimizedShelf::States::Base].defaults.brushSettings[::State::BrushType::Background].color =
      Color::Create(0x00000000);
  });
}


//-------------------------------------------------------------------------------------------------
// Thumb
//-------------------------------------------------------------------------------------------------

MinimizedShelf::Thumb::Thumb(MinimizedShelf *, HWND target)
    : Drawable(L"MinimizedShelf"), mTarget(target), mThumbnail(nullptr), mPlaced({ 0, 0, 0, 0 }) {
  mStateRender.Load(sInitData, mSettings);
  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sThumbDefaults);
  windowSettings.hidden = true;
  mWindow->Initialize(windowSettings, &mStateRender);
  DwmRegisterThumbnail(mWindow->GetWindowHandle(), target, &mThumbnail);
}


MinimizedShelf::Thumb::~Thumb() {
  if (mThumbnail != nullptr) {
    DwmUnregisterThumbnail(mThumbnail);
  }
}


void MinimizedShelf::Thumb::Place(const RECT &rect, int padding) {
  if (!EqualRect(&rect, &mPlaced)) {
    mPlaced = rect;
    mWindow->SetPosition(float(rect.left), float(rect.top), float(rect.right - rect.left), float(rect.bottom - rect.top));

    DWM_THUMBNAIL_PROPERTIES properties = {};
    properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    properties.rcDestination = { padding, padding, rect.right - rect.left - padding, rect.bottom - rect.top - padding };
    properties.fVisible = TRUE;
    properties.opacity = 255;
    properties.fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(mThumbnail, &properties);
  }
  if (!mWindow->IsVisible()) {
    mWindow->Show();
  }
}


LRESULT WINAPI MinimizedShelf::Thumb::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID) {
  if (message == WM_LBUTTONUP) {
    ShowWindowAsync(mTarget, SW_RESTORE);
    WindowActivation::ForceForeground(mTarget);
  }
  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}


//-------------------------------------------------------------------------------------------------
// MinimizedShelf
//-------------------------------------------------------------------------------------------------

MinimizedShelf::MinimizedShelf() : mTimer(0) {
  Settings settings(L"MinimizedShelf");
  mThumbHeight = std::max(16, settings.GetInt(L"ThumbHeight", 110));
  mMaxThumbWidth = std::max(32, settings.GetInt(L"MaxThumbWidth", 220));
  mGap = settings.GetInt(L"Gap", 6);
  mPadding = std::max(0, settings.GetInt(L"Padding", 6));
}


MinimizedShelf::~MinimizedShelf() {
  if (mTimer != 0) {
    KillTimer(nullptr, mTimer);
  }
  mThumbs.clear();
}


void MinimizedShelf::Start() {
  Refresh();
}


bool MinimizedShelf::Accepts(HWND window) const {
  return IsIconic(window) && GetPropW(window, kParkedProp) == nullptr &&
    WindowManager::IsTaskbarWindow(window);
}


bool MinimizedShelf::ComputeRect(HWND window, HTHUMBNAIL thumbnail, RECT &rect) const {
  RECT button;
  if (!WindowManager::GetButtonRect(window, &button)) {
    return false;
  }

  // The window's shape: its own content if DWM knows it, else its restored rectangle.
  float aspect = 4.0f / 3.0f;
  SIZE source = { 0, 0 };
  WINDOWPLACEMENT placement = { sizeof(placement) };
  if (thumbnail != nullptr && SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &source)) &&
      source.cx > 0 && source.cy > 0) {
    aspect = float(source.cx) / float(source.cy);
  } else if (GetWindowPlacement(window, &placement)) {
    const RECT &r = placement.rcNormalPosition;
    if (r.right > r.left && r.bottom > r.top) {
      aspect = float(r.right - r.left) / float(r.bottom - r.top);
    }
  }

  // As wide as the button (within MaxThumbWidth), as high as that width allows (within
  // ThumbHeight).
  const int buttonWidth = button.right - button.left;
  int width = std::min(mMaxThumbWidth, std::max(buttonWidth - 2 * mPadding, mThumbHeight / 2));
  int height = int(width / aspect + 0.5f);
  if (height > mThumbHeight) {
    height = mThumbHeight;
    width = int(height * aspect + 0.5f);
  }
  width += 2 * mPadding;
  height += 2 * mPadding;

  // Centred on the button, on the desktop's side of the taskbar, on the button's monitor.
  MONITORINFO monitor = { sizeof(monitor) };
  GetMonitorInfoW(MonitorFromRect(&button, MONITOR_DEFAULTTONEAREST), &monitor);
  const int centerX = (button.left + button.right) / 2;
  const bool taskbarAtBottom = (button.top + button.bottom) / 2 > (monitor.rcMonitor.top + monitor.rcMonitor.bottom) / 2;
  rect.left = std::max(int(monitor.rcMonitor.left), std::min(centerX - width / 2, int(monitor.rcMonitor.right) - width));
  rect.right = rect.left + width;
  if (taskbarAtBottom) {
    rect.bottom = button.top - mGap;
    rect.top = rect.bottom - height;
  } else {
    rect.top = button.bottom + mGap;
    rect.bottom = rect.top + height;
  }
  return true;
}


void MinimizedShelf::Reposition() {
  for (auto &thumb : mThumbs) {
    RECT rect;
    if (ComputeRect(thumb->GetTarget(), thumb->GetThumbnail(), rect)) {
      thumb->Place(rect, mPadding);
    } else if (thumb->GetWindow()->IsVisible()) {
      // Its button is gone (a refresh in progress, a window no taskbar shows): no place.
      thumb->GetWindow()->Hide();
    }
  }
}


void MinimizedShelf::UpdateTimer() {
  if (!mThumbs.empty() && mTimer == 0) {
    mTimer = SetTimer(nullptr, 0, kRepositionInterval, TimerProc);
  } else if (mThumbs.empty() && mTimer != 0) {
    KillTimer(nullptr, mTimer);
    mTimer = 0;
  }
}


void CALLBACK MinimizedShelf::TimerProc(HWND, UINT, UINT_PTR, DWORD) {
  if (gMinimizedShelf) {
    gMinimizedShelf->Reposition();
  }
}


void MinimizedShelf::Add(HWND window) {
  for (const auto &thumb : mThumbs) {
    if (thumb->GetTarget() == window) {
      return;
    }
  }
  if (!Accepts(window)) {
    return;
  }
  mThumbs.emplace_back(new Thumb(this, window));
  Reposition();
  UpdateTimer();
}


void MinimizedShelf::Remove(HWND window) {
  for (auto thumb = mThumbs.begin(); thumb != mThumbs.end(); ++thumb) {
    if ((*thumb)->GetTarget() == window) {
      mThumbs.erase(thumb);
      UpdateTimer();
      return;
    }
  }
}


void MinimizedShelf::Refresh() {
  for (auto thumb = mThumbs.begin(); thumb != mThumbs.end();) {
    if (!IsWindow((*thumb)->GetTarget()) || !Accepts((*thumb)->GetTarget())) {
      thumb = mThumbs.erase(thumb);
    } else {
      ++thumb;
    }
  }

  // Minimized windows not seen yet (minimized before LiteStep started).
  std::vector<HWND> found;
  EnumWindows([] (HWND window, LPARAM data) -> BOOL {
    if (IsIconic(window)) {
      reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
    }
    return TRUE;
  }, LPARAM(&found));
  for (HWND window : found) {
    bool known = std::any_of(mThumbs.begin(), mThumbs.end(),
      [window] (const std::unique_ptr<Thumb> &thumb) { return thumb->GetTarget() == window; });
    if (!known && Accepts(window)) {
      mThumbs.emplace_back(new Thumb(this, window));
    }
  }

  Reposition();
  UpdateTimer();
}


bool MinimizedShelf::GetSlot(HWND window, LPRECT rect) {
  for (const auto &thumb : mThumbs) {
    if (thumb->GetTarget() == window) {
      return ComputeRect(window, thumb->GetThumbnail(), *rect);
    }
  }
  // Being minimized now: where its thumbnail will go.
  if (IsIconic(window) || !WindowManager::IsTaskbarWindow(window) || GetPropW(window, kParkedProp) != nullptr) {
    return false;
  }
  return ComputeRect(window, nullptr, *rect);
}
