//-------------------------------------------------------------------------------------------------
// /ModuleKit/ShadowWindow.hpp
// The nModules Project
//
// The drop shadow of a top-level window. A window can only paint inside its own rectangle,
// so the shadow is a second window: layered, click-through, a little larger than the shape,
// kept right below its window in the stacking order. It is drawn once per shape (a real
// gaussian blur, in software) and only moved after that.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "State.hpp"
#include "ShapeMask.hpp"

#include <vector>

/// <summary>
/// What a shadow is cast by: the shape of the window's current state.
/// </summary>
struct ShadowShape {
  // The layers (<Prefix>ShadowPreset, <Prefix>Shadow<N>...). Offsets, blur and spread in px.
  std::vector<State::ShadowLayer> layers;
  // The shape, in the window's client coordinates.
  D2D1_RECT_F rect;
  State::CornerRadii radii;
  // For a picture background: its alpha at the window's size, used instead of the rounded
  // rectangle so the shadow follows the picture.
  std::shared_ptr<const ShapeMask> mask;
};


class ShadowWindow {
public:
  ShadowWindow();
  ~ShadowWindow();

  ShadowWindow(const ShadowWindow&) = delete;
  ShadowWindow &operator=(const ShadowWindow&) = delete;

public:
  /// <summary>
  /// Shows the shadow of the given window under it, or hides it (no layers, window hidden,
  /// or show == false). Re-renders only when the shape changed.
  /// </summary>
  void Update(HWND target, const ShadowShape &shape, bool show, float opacity = 1.0f);

  /// <summary>
  /// The class name of shadow windows, so that code stacking windows can skip them.
  /// </summary>
  static const wchar_t *ClassName();

private:
  bool Render(const ShadowShape &shape);
  void Hide();

private:
  HWND mWindow;
  // What the current bitmap was rendered for.
  ShadowShape mRendered;
  bool mHasRendered;
  // Where the shadow starts relative to the window's top left corner.
  POINT mOrigin;
  SIZE mSize;
  // 0..1, the window's own opacity during its animations.
  float mOpacity = 1.0f;
};
