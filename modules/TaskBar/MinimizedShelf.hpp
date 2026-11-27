//-------------------------------------------------------------------------------------------------
// /TaskBar/MinimizedShelf.hpp
// The nModules Project
//
// Minimized windows as thumbnails on the desktop: a row right above the bottom of the work
// area, left to right, each one the window's own content (a DWM thumbnail). Clicking one
// restores its window. Minimizing animates into the window's place in the row.
//
// RC (prefix MinimizedShelf):
//   MinimizedShelfEnabled true|false     default false
//   MinimizedShelfThumbHeight <px>       height of a thumbnail, default 110
//   MinimizedShelfMaxThumbWidth <px>     default 200
//   MinimizedShelfSpacing <px>           between thumbnails, default 12
//   MinimizedShelfPadding <px>           around them, inside the row's background, default 8
//   MinimizedShelfMarginLeft / MarginBottom <px>   from the work area's corner, default 12
//   and every window setting (Color, CornerRadius*, ShadowPreset, ...) for the row itself,
//   which is kept on the desktop (OnDesktop) unless the theme says otherwise.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "../ModuleKit/Drawable.hpp"
#include "../ModuleKit/StateRender.hpp"

#include <dwmapi.h>
#include <memory>
#include <vector>

class MinimizedShelf : public Drawable {
public:
  enum class States {
    Base,
    Count
  };

public:
  MinimizedShelf();
  ~MinimizedShelf();

  /// <summary>Starts following minimized windows (and shows the ones already minimized).</summary>
  void Start();

  /// <summary>A window was minimized.</summary>
  void Add(HWND window);

  /// <summary>A window was restored or closed.</summary>
  void Remove(HWND window);

  /// <summary>Drops windows that are no longer minimized and picks up any that are.</summary>
  void Refresh();

  /// <summary>
  /// Where the window's thumbnail is, or where it will go if it is being minimized now, in
  /// screen coordinates: the target of the minimize animation.
  /// </summary>
  bool GetSlot(HWND window, LPRECT rect);

  LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

private:
  struct Item {
    HWND window;
    HTHUMBNAIL thumbnail;
    int width;
  };

  bool Accepts(HWND window) const;
  int WidthFor(HWND window, HTHUMBNAIL thumbnail) const;
  // The row's position and size, and each thumbnail's place in it, for these widths.
  void ComputeLayout(const std::vector<int> &widths, RECT &row, std::vector<RECT> &slots) const;
  void Layout();

private:
  StateRender<States> mStateRender;
  std::vector<Item> mItems;
  int mThumbHeight;
  int mMaxThumbWidth;
  int mSpacing;
  int mPadding;
  int mMarginLeft;
  int mMarginBottom;
};

// The shelf, when the theme turns it on (MinimizedShelfEnabled true).
extern std::unique_ptr<MinimizedShelf> gMinimizedShelf;
