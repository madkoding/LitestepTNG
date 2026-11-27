//-------------------------------------------------------------------------------------------------
// /Notify/NotificationBox.cpp
// The nModules Project
//
// One notification on the desktop.
//-------------------------------------------------------------------------------------------------
#include "NotificationBox.hpp"

#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include <ctime>

extern LSModule gLSModule;

// The notification's own text is shown as it is: it comes from other programs and must
// not be parsed for [dynamic text] functions.
static const WindowSettings sBoxDefaults([] (WindowSettings &defaults) {
  defaults.width = 240;
  defaults.height = 76;
});

static const WindowSettings sPartDefaults([] (WindowSettings &defaults) {
  defaults.clickThrough = true;
});

static const WindowSettings sCloseDefaults([] (WindowSettings &defaults) {
  defaults.width = 10;
  defaults.height = 10;
});

static const StateRender<NotificationBox::States>::InitData sInitData(
    [] (StateRender<NotificationBox::States>::InitData &initData) {
  initData[NotificationBox::States::Hover].prefix = L"Hover";
  // Nothing is painted unless the theme says so: a part without settings (the click
  // area around the close button) must not show up as a solid block.
  for (auto &state : initData) {
    state.defaults.brushSettings[::State::BrushType::Background].color = Color::Create(0x00000000);
  }
});


/// <summary>
/// Formats a UTC file time as local time with a strftime pattern.
/// </summary>
static std::wstring FormatTime(const FILETIME &time, LPCWSTR format) {
  SYSTEMTIME utc, local;
  if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
    return L"";
  }

  tm parts = {};
  parts.tm_year = local.wYear - 1900;
  parts.tm_mon = local.wMonth - 1;
  parts.tm_mday = local.wDay;
  parts.tm_hour = local.wHour;
  parts.tm_min = local.wMinute;
  parts.tm_sec = local.wSecond;
  parts.tm_wday = local.wDayOfWeek;
  parts.tm_isdst = -1;

  WCHAR buffer[128];
  return wcsftime(buffer, _countof(buffer), format, &parts) != 0 ? buffer : L"";
}


NotificationBox::Part::Part(NotificationBox *box, LPCWSTR name, bool isCloseButton)
    : Drawable(box, name), mBox(box), mIsCloseButton(isCloseButton) {
  mStateRender.Load(sInitData, mSettings);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, isCloseButton ? &sCloseDefaults : &sPartDefaults);
  mWindow->Initialize(windowSettings, &mStateRender);
  mWindow->Show();
}


LRESULT WINAPI NotificationBox::Part::HandleMessage(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, LPVOID) {
  switch (message) {
  case WM_MOUSEMOVE:
    mStateRender.ActivateState(States::Hover, mWindow);
    break;

  case WM_MOUSELEAVE:
    mStateRender.ClearState(States::Hover, mWindow);
    break;

  case WM_LBUTTONUP:
    if (mIsCloseButton) {
      PostMessageW(gLSModule.GetMessageWindow(), WM_NOTIFY_DISMISS, mBox->GetData().id, 0);
      return 0;
    }
    break;
  }

  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}


NotificationBox::NotificationBox(const ToastData &data, LPCWSTR timeFormat)
    : Drawable(L"NotifyBox"), mData(data) {
  mStateRender.Load(sInitData, mSettings);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sBoxDefaults);
  // Stays hidden until the layout has given it a place.
  mWindow->Initialize(windowSettings, &mStateRender);

  mTitle.reset(new Part(this, L"Title", false));
  mText.reset(new Part(this, L"Text", false));
  mTime.reset(new Part(this, L"Time", false));
  // The area first: the button is drawn over it, and both close the box.
  mCloseArea.reset(new Part(this, L"CloseArea", true));
  mClose.reset(new Part(this, L"Close", true));

  std::wstring text = mData.title;
  if (!mData.body.empty()) {
    if (!text.empty()) {
      text += L"\n";
    }
    text += mData.body;
  }

  mTitle->GetWindow()->SetText(mData.app.c_str());
  mText->GetWindow()->SetText(text.c_str());
  mTime->GetWindow()->SetText(FormatTime(mData.time, timeFormat).c_str());
}


void NotificationBox::Place(float x, float y, int moveDuration) {
  D2D1_POINT_2F position = mWindow->GetPosition();
  if (position.x != x || position.y != y) {
    if (mWindow->IsVisible() && moveDuration > 0) {
      // The others close up when one leaves: they slide to their new place.
      const D2D1_SIZE_F size = mWindow->GetSize();
      mWindow->SetAnimation(Distance(x), Distance(y), Distance(size.width), Distance(size.height),
        moveDuration, Easing::Type::Cubic);
    } else {
      mWindow->Move(x, y);
    }
  }
  if (!mWindow->IsVisible()) {
    mWindow->Show();
  }
}


void NotificationBox::Hide() {
  if (mWindow->IsVisible()) {
    mWindow->Hide();
  }
}


LRESULT WINAPI NotificationBox::HandleMessage(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, LPVOID) {
  switch (message) {
  case WM_MOUSEMOVE:
    mStateRender.ActivateState(States::Hover, mWindow);
    break;

  case WM_MOUSELEAVE:
    mStateRender.ClearState(States::Hover, mWindow);
    break;

  case WM_LBUTTONUP:
    // The text parts are click-through, so this is any click that is not on the X.
    PostMessageW(gLSModule.GetMessageWindow(), WM_NOTIFY_ACTIVATE, mData.id, 0);
    break;
  }

  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}
