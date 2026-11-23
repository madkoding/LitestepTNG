//-------------------------------------------------------------------------------------------------
// /Wallpaper/ThemeWallpaper.h
// The nModules Project
//
// Setting the Windows wallpaper, and a theme's own wallpaper.
//-------------------------------------------------------------------------------------------------
#pragma once

namespace ThemeWallpaper {
  /// <summary>
  /// Applies the theme's wallpaper ("ThemeWallpaper <file>", optional
  /// "ThemeWallpaperStyle Center|Tile|Stretch|Fit|Fill|Span"), or, for a theme without
  /// one, gives the user back the wallpaper they had chosen themselves.
  /// Called when the module loads and on !Refresh.
  /// </summary>
  void Apply();

  /// <summary>
  /// Makes a picture the Windows wallpaper. style may be null (keep the current one).
  /// </summary>
  void Set(const wchar_t *path, const wchar_t *style);

  /// <summary>
  /// Changes only how the current wallpaper is fitted. Returns false for an unknown style.
  /// </summary>
  bool SetStyle(const wchar_t *style);
}
