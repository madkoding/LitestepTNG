//-------------------------------------------------------------------------------------------------
// /TaskBar/MinimizedShelf.hpp
// The nModules Project
//
// Minimized windows as thumbnails over their own task buttons: each minimized window gets a
// small window, right above its button (below it for a taskbar at the top of the screen),
// that shows the window's content (a DWM thumbnail). The thumbnails follow their buttons when
// the taskbar lays its buttons out again. Clicking one restores its window, and minimizing
// animates into it.
//
// RC (prefix MinimizedShelf):
//   MinimizedShelfEnabled true|false     default false
//   MinimizedShelfThumbHeight <px>       the most a thumbnail's content may be high, default 110
//   MinimizedShelfMaxThumbWidth <px>     the most it may be wide, default 220; it is as wide as
//                                        its button when the button is narrower
//   MinimizedShelfGap <px>               between the thumbnail and its button, default 6
//   MinimizedShelfPadding <px>           around the content, inside the background, default 6
//   and every window setting (Color, CornerRadius*, ShadowPreset, ShowAnimation*...) for the
//   thumbnails, which are kept on the desktop (OnDesktop) unless the theme says otherwise.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "../ModuleKit/Drawable.hpp"
#include "../ModuleKit/StateRender.hpp"

#include <dwmapi.h>
#include <memory>
#include <vector>

class MinimizedShelf {
public:
  enum class States {
    Base,
    Count
  };

public:
  MinimizedShelf();
  ~MinimizedShelf();

  /// <summary>Shows the windows that are already minimized.</summary>
  void Start();

  /// <summary>A window was minimized.</summary>
  void Add(HWND window);

  /// <summary>A window was restored or closed.</summary>
  void Remove(HWND window);

  /// <summary>Drops windows that are no longer minimized and picks up any that are.</summary>
  void Refresh();

  /// <summary>Moves the thumbnails to where their buttons are now.</summary>
  void Reposition();

  /// <summary>
  /// Where the window's thumbnail is, or will be if it is being minimized now, in screen
  /// coordinates: the target of the minimize animation.
  /// </summary>
  bool GetSlot(HWND window, LPRECT rect);

private:
  // One minimized window's thumbnail.
  class Thumb : public Drawable {
  public:
    Thumb(MinimizedShelf *shelf, HWND target);
    ~Thumb();
    HWND GetTarget() const { return mTarget; }
    HTHUMBNAIL GetThumbnail() const { return mThumbnail; }
    // Puts the window at rect (screen) with the content inside the padding.
    void Place(const RECT &rect, int padding);
    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

  private:
    StateRender<States> mStateRender;
    HWND mTarget;
    HTHUMBNAIL mThumbnail;
    RECT mPlaced;
  };

  bool Accepts(HWND window) const;
  // The thumbnail's window rectangle (screen) for this window, from its task button.
  bool ComputeRect(HWND window, HTHUMBNAIL thumbnail, RECT &rect) const;
  void UpdateTimer();
  static void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD);

private:
  std::vector<std::unique_ptr<Thumb>> mThumbs;
  UINT_PTR mTimer;
  int mThumbHeight;
  int mMaxThumbWidth;
  int mGap;
  int mPadding;
};

// The shelf, when the theme turns it on (MinimizedShelfEnabled true).
extern std::unique_ptr<MinimizedShelf> gMinimizedShelf;
