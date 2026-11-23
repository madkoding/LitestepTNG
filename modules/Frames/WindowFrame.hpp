//-------------------------------------------------------------------------------------------------
// /Frames/WindowFrame.hpp
// The nModules Project
//
// The skinned frame around another program's window: a decoration drawn outside it and
// an interactive bar over its native title bar.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "../ModuleKit/Drawable.hpp"
#include "../ModuleKit/StateRender.hpp"
#include "../ModuleKit/Window.hpp"

#include <memory>

class WindowFrame : public Drawable {
public:
  // Later states win: a hovered button of the active window shows its Hover look.
  enum class States {
    Base,
    Active,
    Hover,
    Count
  };

  enum class Role {
    Title,
    Minimize,
    Maximize,
    Close
  };

  /// <summary>
  /// A piece of the bar; settings prefix = the bar's prefix + its name (FrameTitle,
  /// FrameMin, FrameMax, FrameClose).
  /// </summary>
  class Part : public Drawable {
  public:
    Part(WindowFrame *bar, LPCWSTR name, Role role);
    void SetActive(bool active);
    void SetShown(bool shown);

  private:
    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

  private:
    StateRender<States> mStateRender;
    WindowFrame *mBar;
    Role mRole;
    bool mShown;
  };

  /// <summary>
  /// The decoration around the whole window (settings prefix FrameBody): a second
  /// top-level window that is drawn outside the target, has a hole where the target's
  /// content is, and lets every click through.
  /// </summary>
  class Body : public Drawable {
  public:
    Body();
    void SetActive(bool active);
    // outer and hole in screen coordinates; clip limits what is shown (or null).
    void Place(const RECT &outer, const RECT &hole, const RECT *clip);

    // How far the decoration reaches beyond the hole on each side.
    int insetLeft, insetTop, insetRight, insetBottom;

  private:
    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

  private:
    StateRender<States> mStateRender;
    RECT mOuter, mHole, mClip;
  };

public:
  explicit WindowFrame(HWND target);
  ~WindowFrame();

  /// <summary>
  /// The rectangle (screen coordinates) of a window's native title bar, if it has a
  /// standard one that this module can cover.
  /// </summary>
  static bool GetNativeBarRect(HWND target, RECT &bar);

  HWND GetTarget() const { return mTarget; }

  // Follows the target: position, size, stacking order, title and active state.
  // Returns false if the target no longer has a title bar to cover.
  bool Sync();

  // Puts the bar right above its target in the stacking order.
  void Restack();

  // Acts on the target window, the way its own title bar would.
  void Command(Role role);

private:
  LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;
  void ActivateTarget();

private:
  HWND mTarget;
  bool mActive;

  // Dragging the target by the bar. Armed on the button press; it only becomes a drag
  // once the mouse has moved a little, so a double click stays a double click.
  bool mDragArmed;
  bool mDragging;
  POINT mDragCursor;
  RECT mDragTargetRect;
  D2D1_POINT_2F mDragBarPosition;
  WCHAR mTitle[256];
  StateRender<States> mStateRender;
  // Created after the bar's own window is initialized: a child sizes itself against it.
  std::unique_ptr<Part> mTitlePart, mMinimize, mMaximize, mClose;
  // Null when the theme defines no FrameBody insets: then only the title bar is covered.
  std::unique_ptr<Body> mBody;
};
