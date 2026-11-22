/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  Tooltip.cpp
 *  The nModules Project
 *
 *  A general tooltip window.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "Tooltip.hpp"

#include "../CoreCom/Core.h"

#include "../ModuleKit/LiteStep.h"

#include <algorithm>
#include <cmath>

using InitData = StateRender<Tooltip::States>::InitData;

static const WindowSettings sWindowDefaults([] (WindowSettings & defaults) {
  defaults.width = 150;
  defaults.height = 40;
  defaults.alwaysOnTop = true;
});

static const InitData sStateInitData([](InitData & initData) {
  using States = Tooltip::States;
  initData[States::Base].defaults.brushSettings[State::BrushType::Background].color
    = Color::Create(0xCCFAFAD2);
  initData[States::Base].defaults.brushSettings[State::BrushType::Text].color
    = Color::Create(0xFF000000);
  initData[States::Base].defaults.textOffsetTop = 2;
  initData[States::Base].defaults.textOffsetBottom = 2;
  initData[States::Base].defaults.textOffsetRight = 2;
  initData[States::Base].defaults.textOffsetLeft = 2;
  initData[States::Base].defaults.brushSettings[State::BrushType::Outline].color
    = Color::Create(0xFF000000);
  initData[States::Base].defaults.outlineWidth = 0.75f;
});


/// <summary>
/// Constructor
/// </summary>
Tooltip::Tooltip(LPCTSTR prefix, Settings * parentSettings) : Drawable(prefix, parentSettings) {
  mMaxHeight = mSettings->GetInt(L"MaxHeight", 100);
  mMaxWidth = mSettings->GetInt(L"MaxWidth", 300);
  mOffset = mSettings->GetFloat(L"Offset", 5);
  mPlacement = mSettings->GetEnum<Placement>(L"Placement", {
      { Placement::Left, L"Left" },
      { Placement::Top, L"Top" },
      { Placement::Right, L"Right" },
      { Placement::Bottom, L"Bottom" },
  }, Placement::Top);
  mStateRender.Load(sStateInitData, mSettings);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sWindowDefaults);
  mWindow->Initialize(windowSettings, &mStateRender);
}


/// <summary>
/// Shows the tooltip at the specified position.
/// </summary>
void Tooltip::Show(LPCWSTR text, LPRECT position) {
  mWindow->SetText(text);
  mWindow->SizeToText(mMaxWidth, mMaxHeight);

  // Centered above the target, but kept inside the monitor the target is on. Clamping to
  // the whole virtual desktop is not enough: next to a monitor edge the tooltip would
  // spill onto the neighbouring monitor instead of growing away from the edge.
  RECT limits;
  MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
  if (GetMonitorInfoW(MonitorFromRect(position, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
    limits = monitorInfo.rcMonitor;
  } else {
    limits = nCore::FetchMonitorInfo().GetVirtualDesktop().rect;
  }

  float width = mWindow->GetSize().width;
  float height = mWindow->GetSize().height;
  float x = (float)position->left + ((float)position->right - (float)position->left) / 2.0f - width / 2.0f;
  float y = (float)position->top - height - 5.0f;

  // Whole pixels, so pixel fonts stay sharp.
  x = floorf(std::min<float>(std::max<float>((float)limits.left, x), (float)limits.right - width));
  y = floorf(std::min<float>(std::max<float>((float)limits.top, y), (float)limits.bottom - height));
  mWindow->Move(x, y);

  mWindow->Show();
}


/// <summary>
/// Hides the tooltip
/// </summary>
void Tooltip::Hide() {
  mWindow->Hide();
}


/// <summary>
/// Handles Window messages
/// </summary>
LRESULT WINAPI Tooltip::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID) {
  return DefWindowProc(window, message, wParam, lParam);
}

