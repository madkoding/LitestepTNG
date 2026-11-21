//-------------------------------------------------------------------------------------------------
// /Label/Label.hpp
// The nModules Project
//
// Declaration of the Label class.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "../ModuleKit/Drawable.hpp"
#include "../ModuleKit/StateRender.hpp"
#include "../ModuleKit/Window.hpp"

#include <forward_list>
#include <unordered_map>

class Label : public Drawable {
public:
  explicit Label(LPCWSTR name);
  Label(LPCWSTR name, Drawable * parent);
  ~Label();

  // IDrawable
private:
  LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

public:
  enum class States {
    Base,
    Hover,
    Pressed,
    Count
  };

private:
  void Initalize();

  // Slider support. A label becomes a slider with "<Prefix>Slider Volume|Track"; the knob
  // is another label (normally one of its overlays) named by "<Prefix>SliderHandle".
  void InitSlider();
  bool HandleSliderMessage(UINT message, WPARAM wParam, LPARAM lParam);
  int SliderPermilleFromX(int x);
  void PlaceSliderHandle(int permille);

private:
  StateRender<States> mStateRender;
  std::forward_list<Label> mOverlays;
  int mButtonsPressed;

  // -1 when this label is not a slider, otherwise the "what" passed to nCore's
  // MediaGetPermille/MediaSetPermille (0 = master volume, 1 = track position).
  int mSliderSource;
  WCHAR mSliderHandle[64];
  UINT_PTR mSliderTimer;
  bool mSliderDragging;
  // Timer ticks to skip after the user sets a value, while the source catches up.
  int mSliderHold;
  int (__cdecl *mMediaGet)(int what);
  void (__cdecl *mMediaSet)(int what, int permille);
};

