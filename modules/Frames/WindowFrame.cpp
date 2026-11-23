//-------------------------------------------------------------------------------------------------
// /Frames/WindowFrame.cpp
// The nModules Project
//
// The skinned frame around another program's window: a decoration drawn outside it and
// an interactive bar over its native title bar.
//
// Nothing is injected into the other program: the bar is a window of ours that follows
// it, and what the user does on the bar is passed on as the messages the native title
// bar would have produced.
//-------------------------------------------------------------------------------------------------
#include "WindowFrame.hpp"

#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"
#include "../Utilities/WindowActivation.h"

#include <dwmapi.h>
#include <windowsx.h>

// Undocumented but long-standing: shows a window's system menu at a screen position.
#define WM_POPUPSYSTEMMENU 0x0313

static const WindowSettings sBarDefaults([] (WindowSettings &defaults) {
  // The bar belongs with its window, not above every other one. (In overlay mode the
  // default would be topmost.)
  defaults.alwaysOnTop = false;
});

static const WindowSettings sTitleDefaults([] (WindowSettings &defaults) {
  defaults.alwaysOnTop = false;
  defaults.clickThrough = true;
});

static const WindowSettings sButtonDefaults([] (WindowSettings &defaults) {
  defaults.alwaysOnTop = false;
  defaults.width = 10;
  defaults.height = 10;
});

static const StateRender<WindowFrame::States>::InitData sInitData(
    [] (StateRender<WindowFrame::States>::InitData &initData) {
  initData[WindowFrame::States::Active].prefix = L"Active";
  initData[WindowFrame::States::Hover].prefix = L"Hover";
});


WindowFrame::Part::Part(WindowFrame *bar, LPCWSTR name, Role role)
    : Drawable(bar, name), mBar(bar), mRole(role), mShown(true) {
  mStateRender.Load(sInitData, mSettings);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, role == Role::Title ? &sTitleDefaults : &sButtonDefaults);
  mWindow->Initialize(windowSettings, &mStateRender);
  mWindow->Show();
}


void WindowFrame::Part::SetShown(bool shown) {
  if (shown == mShown) {
    return;
  }
  mShown = shown;
  if (shown) {
    mWindow->Show();
    mWindow->Repaint();
  } else {
    mWindow->Hide();
  }
}


void WindowFrame::Part::SetActive(bool active) {
  if (active) {
    mStateRender.ActivateState(States::Active, mWindow);
  } else {
    mStateRender.ClearState(States::Active, mWindow);
  }
}


LRESULT WINAPI WindowFrame::Part::HandleMessage(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, LPVOID) {
  switch (message) {
  case WM_MOUSEMOVE:
    mStateRender.ActivateState(States::Hover, mWindow);
    break;

  case WM_MOUSELEAVE:
    mStateRender.ClearState(States::Hover, mWindow);
    break;

  case WM_LBUTTONUP:
    if (mRole != Role::Title) {
      mBar->Command(mRole);
      return 0;
    }
    break;
  }

  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}


WindowFrame::Body::Body() : Drawable(L"FrameBody") {
  SetRectEmpty(&mOuter);
  SetRectEmpty(&mHole);
  SetRectEmpty(&mClip);

  insetLeft = mSettings->GetInt(L"InsetLeft", 0);
  insetTop = mSettings->GetInt(L"InsetTop", 0);
  insetRight = mSettings->GetInt(L"InsetRight", 0);
  insetBottom = mSettings->GetInt(L"InsetBottom", 0);

  mStateRender.Load(sInitData, mSettings);
  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sBarDefaults);
  mWindow->Initialize(windowSettings, &mStateRender);

  // Clicks go through to whatever is underneath: the target's own resize borders, or
  // the windows behind it. Across processes that takes a layered, transparent window;
  // answering HTTRANSPARENT only works between windows of one thread.
  HWND window = mWindow->GetWindowHandle();
  SetWindowLongPtrW(window, GWL_EXSTYLE,
    GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
  SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
}


void WindowFrame::Body::SetActive(bool active) {
  if (active) {
    mStateRender.ActivateState(States::Active, mWindow);
  } else {
    mStateRender.ClearState(States::Active, mWindow);
  }
}


void WindowFrame::Body::Place(const RECT &outer, const RECT &hole, const RECT *clip) {
  RECT noClip = { 0, 0, 0, 0 };
  if (clip == nullptr) {
    clip = &noClip;
  }
  if (!EqualRect(&outer, &mOuter) || !EqualRect(&hole, &mHole) || !EqualRect(clip, &mClip)) {
    bool resized = (outer.right - outer.left) != (mOuter.right - mOuter.left)
      || (outer.bottom - outer.top) != (mOuter.bottom - mOuter.top)
      || (hole.left - outer.left) != (mHole.left - mOuter.left)
      || (hole.top - outer.top) != (mHole.top - mOuter.top)
      || (hole.right - hole.left) != (mHole.right - mHole.left)
      || (hole.bottom - hole.top) != (mHole.bottom - mHole.top)
      || !EqualRect(clip, &mClip) || IsRectEmpty(&mOuter);
    mOuter = outer;
    mHole = hole;
    mClip = *clip;

    mWindow->SetPosition(float(outer.left), float(outer.top), float(outer.right - outer.left),
      float(outer.bottom - outer.top));

    if (resized) {
      // The window is the frame only: everything minus the hole (window coordinates).
      // The system owns the region once it is set.
      HRGN region = CreateRectRgn(0, 0, outer.right - outer.left, outer.bottom - outer.top);
      if (!IsRectEmpty(clip)) {
        HRGN visible = CreateRectRgn(clip->left - outer.left, clip->top - outer.top,
          clip->right - outer.left, clip->bottom - outer.top);
        CombineRgn(region, region, visible, RGN_AND);
        DeleteObject(visible);
      }
      HRGN inside = CreateRectRgn(hole.left - outer.left, hole.top - outer.top,
        hole.right - outer.left, hole.bottom - outer.top);
      CombineRgn(region, region, inside, RGN_DIFF);
      DeleteObject(inside);
      SetWindowRgn(mWindow->GetWindowHandle(), region, TRUE);
    }
  }
  if (!mWindow->IsVisible()) {
    mWindow->Show();
  }
}


LRESULT WINAPI WindowFrame::Body::HandleMessage(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, LPVOID) {
  return DefWindowProc(window, message, wParam, lParam);
}


bool WindowFrame::GetNativeBarRect(HWND target, RECT &bar) {
  LONG_PTR style = GetWindowLongPtrW(target, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
  if (!IsWindowVisible(target) || IsIconic(target) || (style & WS_CHILD) != 0
      || (style & WS_CAPTION) != WS_CAPTION || (exStyle & WS_EX_TOOLWINDOW) != 0) {
    return false;
  }

  // Windows parked on another virtual desktop, suspended Store apps...
  int cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(target, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
    return false;
  }

  // Not on any screen: parked on another virtual desktop (the VWM module moves those
  // far to the right), or dragged out of sight. There is nothing to decorate, and the
  // screen tests further down only make sense against the monitor the window is on.
  HMONITOR onMonitor = MonitorFromWindow(target, MONITOR_DEFAULTTONULL);
  if (onMonitor == nullptr) {
    return false;
  }

  TITLEBARINFO info = { sizeof(info) };
  if (!GetTitleBarInfo(target, &info) || (info.rgstate[0] & STATE_SYSTEM_INVISIBLE) != 0) {
    return false;
  }

  // Programs that draw their own title bar (browsers, Terminal, Electron apps, Store
  // apps) keep the WS_CAPTION style but extend their client area over it. Those are
  // left alone: there is no native bar to cover, only the program's own drawing.
  POINT clientTop = { 0, 0 };
  ClientToScreen(target, &clientTop);
  if (clientTop.y < info.rcTitleBar.bottom - 2) {
    return false;
  }

  // The frame as it is drawn (the window rectangle also has invisible resize borders).
  RECT frame;
  if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
    GetWindowRect(target, &frame);
  }

  bar.left = frame.left;
  bar.right = frame.right;
  bar.top = frame.top;
  bar.bottom = info.rcTitleBar.bottom;

  // A maximized window hangs over the edges of its monitor.
  if (IsZoomed(target)) {
    MONITORINFO monitor = { sizeof(monitor) };
    if (GetMonitorInfoW(MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST), &monitor)) {
      if (bar.left < monitor.rcWork.left) bar.left = monitor.rcWork.left;
      if (bar.right > monitor.rcWork.right) bar.right = monitor.rcWork.right;
      if (bar.top < monitor.rcWork.top) bar.top = monitor.rcWork.top;
    }
  }

  // Full screen (a video, a game, a browser in F11): the program has the whole monitor
  // and whatever caption it still has is not meant to be seen. A maximized window can
  // also fill its monitor when nothing is reserved there, hence the IsZoomed test.
  MONITORINFO monitor = { sizeof(monitor) };
  if (GetMonitorInfoW(onMonitor, &monitor)) {
    const RECT &screen = monitor.rcMonitor;
    if (!IsZoomed(target) && frame.left <= screen.left && frame.top <= screen.top
        && frame.right >= screen.right && frame.bottom >= screen.bottom) {
      return false;
    }
    // A caption above the top of the screen is not on show either.
    if (bar.bottom <= screen.top + 4) {
      return false;
    }
  }

  int height = bar.bottom - bar.top;
  return height >= 16 && height <= 80 && bar.right - bar.left >= 120;
}


WindowFrame::WindowFrame(HWND target) : Drawable(L"Frame"), mTarget(target), mActive(false),
    mDragArmed(false), mDragging(false) {
  mTitle[0] = L'\0';
  mStateRender.Load(sInitData, mSettings);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sBarDefaults);
  mWindow->Initialize(windowSettings, &mStateRender);

  mTitlePart.reset(new Part(this, L"Title", Role::Title));
  mMinimize.reset(new Part(this, L"Min", Role::Minimize));
  mMaximize.reset(new Part(this, L"Max", Role::Maximize));
  mClose.reset(new Part(this, L"Close", Role::Close));

  mBody.reset(new Body());
  if (mBody->insetLeft <= 0 && mBody->insetTop <= 0 && mBody->insetRight <= 0 && mBody->insetBottom <= 0) {
    mBody.reset();
  } else {
    // A square frame around rounded corners would show the desktop in the gaps. Windows
    // may refuse this for another program's window; then the corners stay as they are.
    int corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(mTarget, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  }
}


WindowFrame::~WindowFrame() {
  if (mBody && IsWindow(mTarget)) {
    int corners = DWMWCP_DEFAULT;
    DwmSetWindowAttribute(mTarget, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  }
}


bool WindowFrame::Sync() {
  RECT bar;
  if (!GetNativeBarRect(mTarget, bar)) {
    return false;
  }

  // With a body, everything is laid out around the "hole": the part of the target below
  // its title bar. "outer" is the whole decoration; the bar is its top strip, so the
  // theme positions the title and the buttons relative to the decoration's corner.
  RECT outer = { 0, 0, 0, 0 }, hole = { 0, 0, 0, 0 };
  if (mBody) {
    RECT frame;
    if (FAILED(DwmGetWindowAttribute(mTarget, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
      GetWindowRect(mTarget, &frame);
    }

    if (IsZoomed(mTarget)) {
      // No room around a maximized window, and its caption is shorter (the top of the
      // frame hangs off the screen). The decoration becomes just the strip over the
      // caption, starting at the window's own corner so the header art is complete.
      hole.left = bar.left;
      hole.top = bar.bottom;
      hole.right = bar.right;
      hole.bottom = frame.bottom;
      outer = hole;
      outer.top = bar.top;
    } else {
      // The native 1px border goes under the decoration.
      hole.left = bar.left + 1;
      hole.top = bar.bottom;
      hole.right = bar.right - 1;
      hole.bottom = frame.bottom - 1;

      outer.left = hole.left - mBody->insetLeft;
      outer.top = bar.bottom - mBody->insetTop;
      if (outer.top > bar.top) outer.top = bar.top;
      outer.right = hole.right + mBody->insetRight;
      outer.bottom = hole.bottom + mBody->insetBottom;
    }
    bar.left = outer.left;
    bar.top = outer.top;
    bar.right = outer.right;
  }

  D2D1_POINT_2F position = mWindow->GetPosition();
  D2D1_SIZE_F size = mWindow->GetSize();
  // While dragging, the bar leads and the target follows; its reports of where it is
  // are a little behind and would pull the bar back.
  if (mDragging) {
    int dx = LONG(position.x) - bar.left, dy = LONG(position.y) - bar.top;
    OffsetRect(&bar, dx, dy);
    OffsetRect(&outer, dx, dy);
    OffsetRect(&hole, dx, dy);
  }

  if (mBody) {
    mBody->Place(outer, hole, nullptr);
  }  if (position.x != float(bar.left) || position.y != float(bar.top)
      || size.width != float(bar.right - bar.left) || size.height != float(bar.bottom - bar.top)) {
    mWindow->SetPosition(float(bar.left), float(bar.top), float(bar.right - bar.left),
      float(bar.bottom - bar.top));
  }

  // Reads the caption Windows keeps for the window; this never waits on the program.
  WCHAR title[_countof(mTitle)] = L"";
  GetWindowTextW(mTarget, title, _countof(title));
  if (wcscmp(title, mTitle) != 0) {
    wcscpy_s(mTitle, title);
    mTitlePart->GetWindow()->SetText(mTitle);
    mTitlePart->GetWindow()->Repaint();
  }

  // Only offer what the window itself offers: a dialog without a maximize box must not
  // be maximized from here either.
  LONG_PTR style = GetWindowLongPtrW(mTarget, GWL_STYLE);
  mMinimize->SetShown((style & WS_MINIMIZEBOX) != 0);
  mMaximize->SetShown((style & WS_MAXIMIZEBOX) != 0);

  bool active = GetForegroundWindow() == mTarget;
  if (active != mActive) {
    mActive = active;
    if (active) {
      mStateRender.ActivateState(States::Active, mWindow);
    } else {
      mStateRender.ClearState(States::Active, mWindow);
    }
    if (mBody) mBody->SetActive(active);
    mTitlePart->SetActive(active);
    mMinimize->SetActive(active);
    mMaximize->SetActive(active);
    mClose->SetActive(active);
  }

  if (!mWindow->IsVisible()) {
    mWindow->Show();
  }
  Restack();
  return true;
}


void WindowFrame::Restack() {
  // Wanted order, top down: bar, body, target.
  HWND self = mWindow->GetWindowHandle();
  HWND body = mBody ? mBody->GetWindow()->GetWindowHandle() : nullptr;
  HWND above = ::GetWindow(mTarget, GW_HWNDPREV);
  if (body != nullptr) {
    if (above == body && ::GetWindow(body, GW_HWNDPREV) == self) {
      return;
    }
  } else if (above == self) {
    return;
  }
  // The first window above the target that is not one of ours.
  while (above != nullptr && (above == self || above == body)) {
    above = ::GetWindow(above, GW_HWNDPREV);
  }

  bool targetTopmost = (GetWindowLongPtrW(mTarget, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
  HWND insertAfter;
  if (above == nullptr) {
    insertAfter = targetTopmost ? HWND_TOPMOST : HWND_TOP;
  } else if (!targetTopmost && (GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) {
    // The target is the highest ordinary window; going "after" a topmost window could
    // drag the bar into the topmost band.
    insertAfter = HWND_TOP;
  } else {
    insertAfter = above;
  }
  SetWindowPos(self, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (body != nullptr) {
    SetWindowPos(body, self, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}


void WindowFrame::ActivateTarget() {
  if (GetForegroundWindow() == mTarget) {
    return;
  }
  WindowActivation::ForceForeground(mTarget);
}


void WindowFrame::Command(Role role) {
  LONG_PTR style = GetWindowLongPtrW(mTarget, GWL_STYLE);
  WPARAM command;
  switch (role) {
  case Role::Minimize:
    if ((style & WS_MINIMIZEBOX) == 0) return;
    command = SC_MINIMIZE;
    break;
  case Role::Maximize:
    if ((style & WS_MAXIMIZEBOX) == 0) return;
    command = IsZoomed(mTarget) ? SC_RESTORE : SC_MAXIMIZE;
    break;
  case Role::Close:    command = SC_CLOSE; break;
  default: return;
  }
  PostMessageW(mTarget, WM_SYSCOMMAND, command, 0);
}


LRESULT WINAPI WindowFrame::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    LPVOID) {
  switch (message) {
  case WM_LBUTTONDOWN:
    ActivateTarget();
    // Activating raised the target above this bar.
    Restack();
    // The move is done from here. Handing it to the program (WM_NCLBUTTONDOWN on its
    // caption) does not work: its move loop waits for mouse messages, and those keep
    // coming to this bar, which is what the mouse is over.
    if (!IsZoomed(mTarget)) {
      GetCursorPos(&mDragCursor);
      GetWindowRect(mTarget, &mDragTargetRect);
      mDragBarPosition = mWindow->GetPosition();
      mDragArmed = true;
      // Captured for the bar itself, or a button passed over mid-drag would take the moves.
      mWindow->SetMouseCapture(this);
    }
    return 0;

  case WM_MOUSEMOVE:
    if (mDragArmed) {
      POINT cursor;
      GetCursorPos(&cursor);
      int dx = cursor.x - mDragCursor.x, dy = cursor.y - mDragCursor.y;
      if (!mDragging && abs(dx) + abs(dy) < 4) {
        return 0;
      }
      // The program is asked to move without waiting for it (it may be busy); if
      // Windows refuses, the drag ends here instead of moving the bar on its own.
      if (!SetWindowPos(mTarget, nullptr, mDragTargetRect.left + dx, mDragTargetRect.top + dy, 0, 0,
          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)) {
        mDragArmed = false;
        mDragging = false;
        mWindow->ReleaseMouseCapture();
        Sync();
        return 0;
      }
      mDragging = true;
      // The bar moves at once, so it never trails behind the window.
      mWindow->Move(mDragBarPosition.x + float(dx), mDragBarPosition.y + float(dy));
      Sync();
      return 0;
    }
    break;

  case WM_LBUTTONUP:
    if (mDragArmed) {
      mDragArmed = false;
      mDragging = false;
      mWindow->ReleaseMouseCapture();
      Sync();
      return 0;
    }
    break;

  case WM_LBUTTONDBLCLK:
    Command(Role::Maximize);
    return 0;

  case WM_RBUTTONUP:
    {
      POINT cursor;
      GetCursorPos(&cursor);
      ActivateTarget();
      PostMessageW(mTarget, WM_POPUPSYSTEMMENU, 0, MAKELPARAM(cursor.x, cursor.y));
    }
    return 0;
  }

  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}
