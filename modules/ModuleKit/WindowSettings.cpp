/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  WindowSettings.cpp
 *  The nModules Project
 *
 *  Holds all RC settings used by the Window class.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include <strsafe.h>
#include "WindowSettings.hpp"
#include "../Utilities/StringUtils.h"
#include "../Utilities/Unordered1To1Map.hpp"

template <typename SettingType>
using SettingMap = const Unordered1To1Map<
    SettingType,
    LPCTSTR,
    std::hash<SettingType>,
    CaseInsensitive::Hash,
    std::equal_to<SettingType>,
    CaseInsensitive::Equal
>;

// String < -- > D2D1_TEXT_ANTIALIAS_MODE
static SettingMap<D2D1_TEXT_ANTIALIAS_MODE> textAntiAliasModeMap(
{
    { D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE, L"ClearType" },
    { D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE, L"GrayScale" },
    { D2D1_TEXT_ANTIALIAS_MODE_ALIASED,   L"Aliased"   }
});


/// <summary>
/// Initalizes the class to all default settings.
/// </summary>
WindowSettings::WindowSettings()
    // LiteStep runs on top of Explorer, so its windows stay above Explorer's by default.
    : alwaysOnTop(true)
    , blurBehind(false)
    , clickThrough(false)
    , evaluateText(false)
    , height(100)
    , hidden(false)
    , onDesktop(false)
    , registerWithCore(false)
    , showAnimationDuration(0)
    , showAnimationOffset(0)
    , showAnimationOffsetX(0)
    , hideAnimationDuration(0)
    , hideAnimationOffset(0)
    , hideAnimationOffsetX(0)
    , textAntiAliasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE)
    , textUppercase(false)
    , width(100)
    , x(0)
    , y(0)
{
    *text = L'\0';
}


/// <summary>
/// Initalizes the class to all default settings.
/// </summary>
WindowSettings::WindowSettings(void init(WindowSettings &))
    : WindowSettings()
{
    init(*this);
}


/// <summary>
/// Loads settings from an RC file using the specified defaults.
/// </summary>
void WindowSettings::Load(const Settings *settings, const WindowSettings *defaults) {
  WCHAR buf[MAX_LINE_LENGTH];
  if (!defaults) {
    defaults = this;
  }

  this->alwaysOnTop = settings->GetBool(L"AlwaysOnTop", defaults->alwaysOnTop);
  this->blurBehind = settings->GetBool(L"BlurBehind", defaults->blurBehind);
  this->clickThrough = settings->GetBool(L"ClickThrough", defaults->clickThrough);
  this->evaluateText = defaults->evaluateText;
  this->height = settings->GetDistance(L"Height", defaults->height);
  this->hidden = settings->GetBool(L"Hidden", defaults->hidden);
  this->onDesktop = settings->GetBool(L"OnDesktop", defaults->onDesktop);
  if (this->onDesktop) {
    // In overlay mode windows default to topmost; a desktop window is the opposite.
    this->alwaysOnTop = false;
  }
  this->registerWithCore = defaults->registerWithCore;
  this->showAnimationDuration = settings->GetInt(L"ShowAnimationDuration", defaults->showAnimationDuration);
  this->showAnimationOffset = settings->GetInt(L"ShowAnimationOffset", defaults->showAnimationOffset);
  this->showAnimationOffsetX = settings->GetInt(L"ShowAnimationOffsetX", defaults->showAnimationOffsetX);
  this->hideAnimationDuration = settings->GetInt(L"HideAnimationDuration", defaults->hideAnimationDuration);
  this->hideAnimationOffset = settings->GetInt(L"HideAnimationOffset", defaults->hideAnimationOffset);
  this->hideAnimationOffsetX = settings->GetInt(L"HideAnimationOffsetX", defaults->hideAnimationOffsetX);
  settings->GetString(L"Text", buf, _countof(buf), defaults->text);
  StringCchCopy(this->text, _countof(this->text), buf);
  settings->GetString(L"TextAntiAliasMode", buf, _countof(buf), textAntiAliasModeMap.GetByA(defaults->textAntiAliasMode, L"ClearType"));
  this->textAntiAliasMode = ParseAntiAliasMode(buf);
  // Per-element "TextUppercase" with a global fallback: setting the bare
  // "TextUppercase" key once makes every drawable render upper case.
  this->textUppercase = settings->GetBool(L"TextUppercase",
    defaults->textUppercase || (GetRCBoolDefW(L"TextUppercase", FALSE) != FALSE));
  this->width = settings->GetDistance(L"Width", defaults->width);
  this->x = settings->GetDistance(L"X", defaults->x);
  this->y = settings->GetDistance(L"Y", defaults->y);
}


/// <summary>
/// Parses 
/// </summary>
D2D1_TEXT_ANTIALIAS_MODE WindowSettings::ParseAntiAliasMode(LPCTSTR str)
{
    return textAntiAliasModeMap.GetByB(str, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}


