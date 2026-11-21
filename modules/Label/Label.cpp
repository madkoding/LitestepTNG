//-------------------------------------------------------------------------------------------------
// /Label/Label.cpp
// The nModules Project
//
// Implementation of the Label class.
//-------------------------------------------------------------------------------------------------
#include "Label.hpp"

#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include "../Utilities/StringUtils.h"

#include <cmath>
#include <unordered_map>
#include <windowsx.h>

extern StringKeyedMaps<std::wstring, Label*>::UnorderedMap gAllLabels;

// Default settings for labels.
static const WindowSettings sLabelDefaults([] (WindowSettings &defaults) {
  defaults.evaluateText = true;
  defaults.registerWithCore = true;
});

// StateRender initialization information.
static const StateRender<Label::States>::InitData sInitData([] (StateRender<Label::States>::InitData &initData) {
  initData[Label::States::Hover].prefix = L"Hover";
  initData[Label::States::Pressed].prefix = L"Pressed";
});


/// <summary>
/// Constructor for top-level labels.
/// </summary>
/// <param name="name">The RC settings prefix of the label.</param>
Label::Label(LPCWSTR name) : Drawable(name) {
  Initalize();
}


/// <summary>
/// Constructor for overlays.
/// </summary>
/// <param name="name">The RC settings prefix of the label.</param>
/// <param name="parent">The overlay label's parent.</param>
Label::Label(LPCWSTR name, Drawable *parent) : Drawable(parent, name, true) {
  Initalize();
}


/// <summary>
/// Destructor
/// </summary>
Label::~Label() {
  if (mSliderTimer != 0) {
    mWindow->ClearCallbackTimer(mSliderTimer);
  }
  mOverlays.clear();
  gAllLabels.erase(mSettings->GetPrefix());
}


/// <summary>
/// Handles messages to the label window.
/// </summary>
/// <param name="window">The window the message is for.</param>
/// <param name="message">The type of message.</param>
/// <param name="wParam">wParam</param>
/// <param name="lParam">lParam</param>
/// <param name="extra">Extra data</param>
LRESULT WINAPI Label::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID extra) {
  UNREFERENCED_PARAMETER(extra);

  if (mSliderSource >= 0 && HandleSliderMessage(message, wParam, lParam)) {
    return 0;
  }

  switch (message) {
  case WM_MOUSEMOVE:
    mStateRender.ActivateState(States::Hover, mWindow);
    break;

  case WM_MOUSELEAVE:
    mButtonsPressed = 0;
    mStateRender.ClearState(States::Hover, mWindow);
    mStateRender.ClearState(States::Pressed, mWindow);
    break;

  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_XBUTTONDOWN:
    ++mButtonsPressed;
    mStateRender.ActivateState(States::Pressed, mWindow);
    break;

  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  case WM_XBUTTONUP:
    if (--mButtonsPressed <= 0) {
      mStateRender.ClearState(States::Pressed, mWindow);
    }
    break;
  }

  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}


/// <summary>
/// Initializes the label.
/// </summary>
void Label::Initalize() {
  mButtonsPressed = 0;
  // Messages can arrive while the window is being set up, before InitSlider runs.
  mSliderSource = -1;
  mSliderTimer = 0;

  gAllLabels.emplace(mSettings->GetPrefix(), this);
    
  mStateRender.Load(sInitData, mSettings);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sLabelDefaults);
  mWindow->Initialize(windowSettings, &mStateRender);
    
  LiteStep::IterateOverCommandLineTokens(mSettings->GetPrefix(), L"OverlayLabel", [this] (LPCWSTR name) -> void {
    mOverlays.emplace_front(name, this);
  });

  if (!mWindow->GetDrawingSettings()->hidden) {
    mWindow->Show();
  }

  InitSlider();
}


/// <summary>
/// Turns the label into a slider if "<Prefix>Slider" names a source.
/// </summary>
void Label::InitSlider() {
  mSliderSource = -1;
  mSliderHandle[0] = L'\0';
  mSliderTimer = 0;
  mSliderDragging = false;
  mSliderHold = 0;
  mMediaGet = nullptr;
  mMediaSet = nullptr;

  WCHAR source[32];
  mSettings->GetString(L"Slider", source, _countof(source), L"");
  if (_wcsicmp(source, L"Volume") == 0) {
    mSliderSource = 0;
  } else if (_wcsicmp(source, L"Track") == 0) {
    mSliderSource = 1;
  } else {
    return;
  }
  mSettings->GetString(L"SliderHandle", mSliderHandle, _countof(mSliderHandle), L"");

  // The values live in nCore, which is always loaded before any label exists.
  if (HMODULE core = GetModuleHandleW(L"Core.dll")) {
    mMediaGet = (int (__cdecl *)(int))GetProcAddress(core, "MediaGetPermille");
    mMediaSet = (void (__cdecl *)(int, int))GetProcAddress(core, "MediaSetPermille");
  }

  mSliderTimer = mWindow->SetCallbackTimer(500, this);
  PlaceSliderHandle(mMediaGet ? mMediaGet(mSliderSource) : -1);
}


/// <summary>
/// Maps an x coordinate (in the coordinates mouse messages arrive in, i.e. relative to
/// the top-level window) to a slider value in 0..1000. The knob's centre follows the
/// mouse, so the usable travel is the label's width minus the knob's width.
/// </summary>
int Label::SliderPermilleFromX(int x) {
  D2D1_RECT_F area = mWindow->GetDrawingRect();
  float knobWidth = 0.0f;
  auto handle = gAllLabels.find(mSliderHandle);
  if (handle != gAllLabels.end()) {
    knobWidth = handle->second->mWindow->GetSize().width;
  }

  float travel = (area.right - area.left) - knobWidth;
  if (travel <= 0.0f) {
    return 0;
  }
  float fraction = (float(x) - area.left - knobWidth / 2.0f) / travel;
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  return int(fraction * 1000.0f + 0.5f);
}


/// <summary>
/// Moves the knob to the position for the given value; an unknown value (-1) parks it
/// at the start.
/// </summary>
void Label::PlaceSliderHandle(int permille) {
  auto handle = gAllLabels.find(mSliderHandle);
  if (handle == gAllLabels.end()) {
    return;
  }
  Window *knob = handle->second->mWindow;

  if (permille < 0) permille = 0;
  if (permille > 1000) permille = 1000;
  float travel = mWindow->GetSize().width - knob->GetSize().width;
  if (travel < 0.0f) travel = 0.0f;
  float x = floorf(travel * float(permille) / 1000.0f + 0.5f);

  D2D1_POINT_2F position = knob->GetPosition();
  if (position.x != x) {
    knob->Move(x, position.y);
  }
}


/// <summary>
/// Slider behaviour: follows the source on a timer, and lets the user click or drag.
/// Volume is applied while dragging; a track seek only on release, so the player does
/// not get a burst of seeks.
/// </summary>
/// <returns>True if the message was consumed.</returns>
bool Label::HandleSliderMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_TIMER:
    if (wParam != mSliderTimer) {
      return false;
    }
    if (mSliderDragging) {
      return true;
    }
    if (mSliderHold > 0) {
      --mSliderHold;
      return true;
    }
    PlaceSliderHandle(mMediaGet ? mMediaGet(mSliderSource) : -1);
    return true;

  case WM_LBUTTONDOWN:
    mSliderDragging = true;
    mWindow->SetMouseCapture();
    // fall through: a click already sets the value
  case WM_MOUSEMOVE:
    if (mSliderDragging) {
      int permille = SliderPermilleFromX(GET_X_LPARAM(lParam));
      PlaceSliderHandle(permille);
      if (mSliderSource == 0 && mMediaSet) {
        mMediaSet(mSliderSource, permille);
      }
      return true;
    }
    return false;

  case WM_LBUTTONUP:
    if (mSliderDragging) {
      mSliderDragging = false;
      mWindow->ReleaseMouseCapture();
      int permille = SliderPermilleFromX(GET_X_LPARAM(lParam));
      PlaceSliderHandle(permille);
      if (mMediaSet) {
        mMediaSet(mSliderSource, permille);
      }
      mSliderHold = 3;
      return true;
    }
    return false;
  }
  return false;
}

