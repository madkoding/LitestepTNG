//-------------------------------------------------------------------------------------------------
// /Wallpaper/DesktopBackground.h
// The nModules Project
//
// The desktop's background when Explorer does not run (LiteStep as the session's shell).
//-------------------------------------------------------------------------------------------------
#pragma once

namespace DesktopBackground {
  /// <summary>
  /// Creates the background window if there is no Explorer desktop (no Progman window):
  /// one window under every other one, over the whole virtual screen, that paints the
  /// Windows wallpaper with its style and runs DesktopRightClick / DesktopMiddleClick.
  /// With Explorer running it does nothing: Explorer paints the desktop.
  /// </summary>
  void Create();

  /// <summary>
  /// Destroys the background window, if it was created.
  /// </summary>
  void Destroy();
}
