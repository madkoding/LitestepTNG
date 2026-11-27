//-------------------------------------------------------------------------------------------------
// /TaskBar/MinimizedShelf.cpp
// The nModules Project
//
// See MinimizedShelf.hpp.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"
#include "../ModuleKit/FrameClock.hpp"
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

  // The content's size when it starts growing out of the button (share of the full size).
  const float kStartScale = 0.55f;

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
    : Drawable(L"MinimizedShelf"), mTarget(target), mThumbnail(nullptr), mPlaced({ 0, 0, 0, 0 }),
      mPadding(0), mAtBottom(true), mGone(false), mAnimation(Animation::None) {
  mStateRender.Load(sInitData, mSettings);
  mShowDuration = std::max(0, mSettings->GetInt(L"ShowAnimationDuration", 0));
  mHideDuration = std::max(0, mSettings->GetInt(L"HideAnimationDuration", 0));
  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sThumbDefaults);
  windowSettings.hidden = true;
  mWindow->Initialize(windowSettings, &mStateRender);
  DwmRegisterThumbnail(mWindow->GetWindowHandle(), target, &mThumbnail);
}


MinimizedShelf::Thumb::~Thumb() {
  if (mAnimation != Animation::None) {
    mWindow->StopFrames(this);
  }
  if (mThumbnail != nullptr) {
    DwmUnregisterThumbnail(mThumbnail);
  }
}


void MinimizedShelf::Thumb::Place(const RECT &rect, int padding, bool atBottom) {
  if (mAnimation == Animation::Out) {
    return;
  }
  const bool appearing = !mWindow->IsVisible();
  if (!EqualRect(&rect, &mPlaced) || padding != mPadding || atBottom != mAtBottom) {
    mPlaced = rect;
    mPadding = padding;
    mAtBottom = atBottom;
    mWindow->SetPosition(float(rect.left), float(rect.top), float(rect.right - rect.left), float(rect.bottom - rect.top));
    if (mAnimation == Animation::None && !appearing) {
      ApplyThumbnail(1.0f);
    }
  }
  if (appearing) {
    ApplyThumbnail(mShowDuration > 0 ? 0.0f : 1.0f);
    mWindow->Show();
    StartAnimation(Animation::In);
  }
}


void MinimizedShelf::Thumb::Close() {
  if (mAnimation == Animation::Out || mGone) {
    return;
  }
  if (!mWindow->IsVisible() || mHideDuration <= 0) {
    mWindow->Hide();
    mGone = true;
    return;
  }
  StartAnimation(Animation::Out);
  // The background fades and slides with the window's own exit animation.
  mWindow->HideAnimated(nullptr);
}


void MinimizedShelf::Thumb::StartAnimation(Animation animation) {
  const int duration = animation == Animation::In ? mShowDuration : mHideDuration;
  if (duration <= 0) {
    ApplyThumbnail(animation == Animation::In ? 1.0f : 0.0f);
    if (animation == Animation::Out) {
      mWindow->Hide();
      mGone = true;
    }
    return;
  }
  if (mAnimation == Animation::None) {
    mWindow->RequestFrames(this);
  }
  mAnimation = animation;
  mAnimationClock.Clock();
  StepAnimation();
}


void MinimizedShelf::Thumb::StepAnimation() {
  const int duration = mAnimation == Animation::In ? mShowDuration : mHideDuration;
  const float t = std::min(1.0f, float(mAnimationClock.GetTime() * 1000.0 / duration));
  if (mAnimation == Animation::In) {
    // Ease-out cubic, like the window's own entry.
    ApplyThumbnail(1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t));
  } else {
    // Ease-in cubic, like the window's own exit.
    ApplyThumbnail(1.0f - t * t * t);
  }
  if (t >= 1.0f) {
    if (mAnimation == Animation::Out) {
      mGone = true;
    }
    mAnimation = Animation::None;
    mWindow->StopFrames(this);
  }
}


void MinimizedShelf::Thumb::ApplyThumbnail(float shown) {
  // The full content box, inside the padding, in the window's coordinates.
  const float width = float(mPlaced.right - mPlaced.left - 2 * mPadding);
  const float height = float(mPlaced.bottom - mPlaced.top - 2 * mPadding);
  const float scale = kStartScale + (1.0f - kStartScale) * shown;
  const float w = width * scale, h = height * scale;
  // Centred across, and pinned to the button's side, so it grows out of the button.
  const float left = mPadding + (width - w) / 2.0f;
  const float top = mAtBottom ? mPadding + height - h : float(mPadding);

  DWM_THUMBNAIL_PROPERTIES properties = {};
  properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
  properties.rcDestination = { int(left + 0.5f), int(top + 0.5f), int(left + w + 0.5f), int(top + h + 0.5f) };
  properties.fVisible = TRUE;
  properties.opacity = BYTE(255.0f * std::max(0.0f, std::min(1.0f, shown)) + 0.5f);
  properties.fSourceClientAreaOnly = FALSE;
  DwmUpdateThumbnailProperties(mThumbnail, &properties);
}


LRESULT WINAPI MinimizedShelf::Thumb::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID) {
  if (message == FrameClock::Message()) {
    if (mAnimation != Animation::None) {
      StepAnimation();
    }
    return 0;
  }
  // A thumbnail on its way out is no longer its window's.
  if (mAnimation == Animation::Out || mGone) {
    return DefWindowProc(window, message, wParam, lParam);
  }
  // On the press: the release never reaches the thumbnail (something takes the mouse in
  // between, seen with a log on real clicks), and a posted test click does not show it.
  if (message == WM_LBUTTONDOWN) {
    // What a task button does for a minimized window (TaskButton::HandleMessage): the window
    // restores itself (some programs, packaged apps and Explorer among them, do not answer
    // a ShowWindow from outside), then it gets the foreground.
    BringWindowToTop(mTarget);
    PostMessage(mTarget, WM_SYSCOMMAND, SC_RESTORE, 0);
    WindowActivation::ForceForeground(mTarget);
    return 0;
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
  mClosing.clear();
}


void MinimizedShelf::Retire(std::unique_ptr<Thumb> thumb) {
  thumb->Close();
  mClosing.push_back(std::move(thumb));
}


void MinimizedShelf::SweepClosing() {
  mClosing.erase(std::remove_if(mClosing.begin(), mClosing.end(),
    [] (const std::unique_ptr<Thumb> &thumb) { return thumb->IsGone(); }), mClosing.end());
}


void MinimizedShelf::Start() {
  Refresh();
}


bool MinimizedShelf::Accepts(HWND window) const {
  return IsIconic(window) && GetPropW(window, kParkedProp) == nullptr &&
    WindowManager::IsTaskbarWindow(window);
}


bool MinimizedShelf::ComputeRect(HWND window, HTHUMBNAIL thumbnail, RECT &rect, bool *atBottom) const {
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
  if (atBottom != nullptr) {
    *atBottom = taskbarAtBottom;
  }
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
    bool atBottom = true;
    if (ComputeRect(thumb->GetTarget(), thumb->GetThumbnail(), rect, &atBottom)) {
      thumb->Place(rect, mPadding, atBottom);
    } else if (thumb->GetWindow()->IsVisible()) {
      // Its button is gone (a refresh in progress, a window no taskbar shows): no place.
      thumb->GetWindow()->Hide();
    }
  }
}


void MinimizedShelf::UpdateTimer() {
  const bool needed = !mThumbs.empty() || !mClosing.empty();
  if (needed && mTimer == 0) {
    mTimer = SetTimer(nullptr, 0, kRepositionInterval, TimerProc);
  } else if (!needed && mTimer != 0) {
    KillTimer(nullptr, mTimer);
    mTimer = 0;
  }
}


void CALLBACK MinimizedShelf::TimerProc(HWND, UINT, UINT_PTR, DWORD) {
  if (gMinimizedShelf) {
    gMinimizedShelf->SweepClosing();
    gMinimizedShelf->Reposition();
    gMinimizedShelf->UpdateTimer();
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
      std::unique_ptr<Thumb> leaving = std::move(*thumb);
      mThumbs.erase(thumb);
      Retire(std::move(leaving));
      SweepClosing();
      UpdateTimer();
      return;
    }
  }
}


void MinimizedShelf::Refresh() {
  for (auto thumb = mThumbs.begin(); thumb != mThumbs.end();) {
    if (!IsWindow((*thumb)->GetTarget()) || !Accepts((*thumb)->GetTarget())) {
      std::unique_ptr<Thumb> leaving = std::move(*thumb);
      thumb = mThumbs.erase(thumb);
      Retire(std::move(leaving));
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
