//-------------------------------------------------------------------------------------------------
// /Notify/NotificationBox.hpp
// The nModules Project
//
// One notification on the desktop: a top-level window with the app name, the text, the
// time it arrived and a close button.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "../ModuleKit/Drawable.hpp"
#include "../ModuleKit/StateRender.hpp"
#include "../ModuleKit/Window.hpp"

#include "Listener.hpp"

#include <memory>

// Posted to the module's message window when a close button is clicked. wParam: the id.
// Posted rather than called, because the click is still being handled by the box that
// is about to be destroyed.
#define WM_NOTIFY_DISMISS (WM_APP + 0x3A02)

// Posted when the box itself is clicked. wParam: the id.
#define WM_NOTIFY_ACTIVATE (WM_APP + 0x3A03)

class NotificationBox : public Drawable {
public:
  enum class States {
    Base,
    Hover,
    Count
  };

  /// <summary>
  /// A piece of the box. Its settings prefix is the box's prefix plus its name, e.g.
  /// NotifyBoxTitle, NotifyBoxText, NotifyBoxTime, NotifyBoxClose, NotifyBoxCloseArea.
  /// </summary>
  class Part : public Drawable {
  public:
    Part(NotificationBox *box, LPCWSTR name, bool isCloseButton);

  private:
    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

  private:
    StateRender<States> mStateRender;
    NotificationBox *mBox;
    bool mIsCloseButton;
  };

public:
  NotificationBox(const ToastData &data, LPCWSTR timeFormat);

  const ToastData &GetData() const { return mData; }
  float GetHeight() const { return mWindow->GetSize().height; }
  float GetWidth() const { return mWindow->GetSize().width; }

  // Puts the box at a screen position and makes sure it is visible.
  // Moves the box to (x, y): a visible box slides there in moveDuration ms.
  void Place(float x, float y, int moveDuration);
  void Hide();

private:
  LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

private:
  ToastData mData;
  StateRender<States> mStateRender;
  // Created after the box's own window is initialized: a child sizes itself against it.
  // mCloseArea is an invisible, larger target around the close button: the button's
  // picture is small, and a click that just misses it would land on the box, which
  // opens the program instead of only closing the notification.
  std::unique_ptr<Part> mTitle, mText, mTime, mCloseArea, mClose;
};
