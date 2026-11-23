//-------------------------------------------------------------------------------------------------
// /Wallpaper/ThemeWallpaper.cpp
// The nModules Project
//
// Setting the Windows wallpaper, and a theme's own wallpaper.
//
// The wallpaper belongs to Windows: Explorer draws it. A theme may bring its own picture;
// a theme that does not must leave the user's choice alone. So when a theme's picture is
// put up, the picture the user had is remembered, and it comes back as soon as a theme
// without a wallpaper is loaded. Whatever the user picks in Windows while such a theme
// is active is theirs and is never touched.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"

#include "ThemeWallpaper.h"

#include <Shlwapi.h>
#include <string>
#include <thread>

static const WCHAR kStateKey[] = L"Software\\LiteStepTNG";
// The picture a theme put up, while it is still the wallpaper.
static const WCHAR kAppliedValue[] = L"ThemeWallpaperApplied";
// The picture the user had before a theme replaced it.
static const WCHAR kUserValue[] = L"UserWallpaper";
static const WCHAR kDesktopKey[] = L"Control Panel\\Desktop";


static std::wstring ReadState(LPCWSTR value) {
  WCHAR buffer[MAX_PATH] = L"";
  DWORD size = sizeof(buffer);
  RegGetValueW(HKEY_CURRENT_USER, kStateKey, value, RRF_RT_REG_SZ, nullptr, buffer, &size);
  return buffer;
}


static void WriteState(LPCWSTR value, const std::wstring &text) {
  RegSetKeyValueW(HKEY_CURRENT_USER, kStateKey, value, REG_SZ, text.c_str(),
    DWORD((text.size() + 1) * sizeof(WCHAR)));
}


static std::wstring CurrentWallpaper() {
  WCHAR buffer[MAX_PATH] = L"";
  SystemParametersInfoW(SPI_GETDESKWALLPAPER, _countof(buffer), buffer, 0);
  return buffer;
}


static bool SamePath(const std::wstring &a, const std::wstring &b) {
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}


/// <summary>
/// Writes the fit style where Windows keeps it. It takes effect with the next wallpaper
/// change. Returns false for a name that is not a style.
/// </summary>
static bool WriteStyle(LPCWSTR style) {
  static const struct { LPCWSTR name; LPCWSTR style; LPCWSTR tile; } styles[] = {
    { L"Center", L"0", L"0" }, { L"Tile", L"0", L"1" }, { L"Stretch", L"2", L"0" },
    { L"Fit", L"6", L"0" }, { L"Fill", L"10", L"0" }, { L"Span", L"22", L"0" }
  };
  for (auto &entry : styles) {
    if (_wcsicmp(style, entry.name) == 0) {
      RegSetKeyValueW(HKEY_CURRENT_USER, kDesktopKey, L"WallpaperStyle", REG_SZ, entry.style,
        DWORD((wcslen(entry.style) + 1) * sizeof(WCHAR)));
      RegSetKeyValueW(HKEY_CURRENT_USER, kDesktopKey, L"TileWallpaper", REG_SZ, entry.tile,
        DWORD((wcslen(entry.tile) + 1) * sizeof(WCHAR)));
      return true;
    }
  }
  return false;
}


void ThemeWallpaper::Set(const wchar_t *path, const wchar_t *style) {
  if (style != nullptr && *style != L'\0') {
    WriteStyle(style);
  }
  // Windows applies the change and tells every program about it; a program that is not
  // answering would hold that up, so it does not happen on the shell's thread. Writing
  // the registry value and broadcasting by hand, as this module used to, does not make
  // Explorer redraw anything.
  std::thread([picture = std::wstring(path)] () mutable {
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, &picture[0], SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
  }).detach();
}


bool ThemeWallpaper::SetStyle(const wchar_t *style) {
  if (!WriteStyle(style)) {
    return false;
  }
  std::wstring current = CurrentWallpaper();
  if (!current.empty()) {
    Set(current.c_str(), nullptr);
  }
  return true;
}


void ThemeWallpaper::Apply() {
  WCHAR setting[MAX_PATH] = L"", themePicture[MAX_PATH] = L"", style[32] = L"";
  GetRCStringW(L"ThemeWallpaper", setting, L"", _countof(setting));
  GetRCStringW(L"ThemeWallpaperStyle", style, L"", _countof(style));
  ExpandEnvironmentStringsW(setting, themePicture, _countof(themePicture));

  const std::wstring current = CurrentWallpaper();
  const std::wstring applied = ReadState(kAppliedValue);

  if (themePicture[0] != L'\0' && PathFileExistsW(themePicture)) {
    if (SamePath(current, themePicture)) {
      WriteState(kAppliedValue, themePicture);
      return;
    }
    // What is up now is the user's own, unless it is what the previous theme put there.
    if (!current.empty() && !SamePath(current, applied)) {
      WriteState(kUserValue, current);
    }
    WriteState(kAppliedValue, themePicture);
    Set(themePicture, style);
    return;
  }

  // This theme has no wallpaper. If a theme's picture is still up, the user gets theirs
  // back; if they have picked something else since, that stays.
  if (!applied.empty()) {
    const std::wstring user = ReadState(kUserValue);
    if (SamePath(current, applied) && !user.empty() && PathFileExistsW(user.c_str())) {
      Set(user.c_str(), nullptr);
    }
    WriteState(kAppliedValue, L"");
  }
}
