//-------------------------------------------------------------------------------------------------
// /Wallpaper/Bangs.cpp
// The nModules Project
//
// Manages nWallpaper specific bang commmands.
//-------------------------------------------------------------------------------------------------
#include "Bangs.h"

#include "../CoreCom/Core.h"

#include "../ModuleKit/LiteStep.h"

#include "ThemeWallpaper.h"

struct BangItem {
  BangItem(LPCWSTR name, LiteStep::BANGCOMMANDPROC handler) {
    this->name = name;
    this->handler = handler;
  }
  LPCWSTR name;
  LiteStep::BANGCOMMANDPROC handler;
};

static const BangItem bangMap[] = {
  // !SetWallpaper "<file>" [Center|Tile|Stretch|Fit|Fill|Span]
  BangItem(L"!SetWallpaper", [] (HWND, LPCWSTR args) {
    WCHAR file[MAX_PATH] = L"", style[MAX_PATH] = L"";
    LPWSTR bufs[] = { file, style };
    LiteStep::CommandTokenize(args, bufs, _countof(bufs), nullptr);
    if (file[0] != L'\0') {
      ThemeWallpaper::Set(file, style);
    }
  }),
  // !SetWallpaperStyle Center|Tile|Stretch|Fit|Fill|Span
  BangItem(L"!SetWallpaperStyle", [] (HWND, LPCWSTR args) {
    WCHAR style[MAX_PATH] = L"";
    LPWSTR bufs[] = { style };
    LiteStep::CommandTokenize(args, bufs, _countof(bufs), nullptr);
    ThemeWallpaper::SetStyle(style);
  })
};

/// <summary>
/// Registers bangs.
/// </summary>
void Bangs::_Register() {
  for (const BangItem &bangItem : bangMap) {
    LiteStep::AddBangCommand(bangItem.name, bangItem.handler);
  }
}


/// <summary>
/// Unregisters bangs.
/// </summary>
void Bangs::_Unregister() {
  for (const BangItem &bangItem : bangMap) {
    LiteStep::RemoveBangCommand(bangItem.name);
  }
}

