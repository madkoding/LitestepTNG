//-------------------------------------------------------------------------------------------------
// /VWM/Transition.h
// The nModules Project
//
// The effect shown when switching desktops.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <Windows.h>

namespace Transition {
  /// <summary>
  /// Registers the window class and reads VWMTransition / VWMTransitionDuration /
  /// VWMTransitionDistance.
  /// </summary>
  void Load(HINSTANCE instance);
  void Unload(HINSTANCE instance);

  /// <summary>
  /// Call just before the desktop changes: freezes what is on screen. The picture then
  /// slides and fades away on its own, driven by a timer on "timerWindow".
  /// </summary>
  /// <param name="direction">-1: the old desktop leaves to the left, +1: to the right.</param>
  void Begin(HWND timerWindow, int direction);

  /// <summary>
  /// Handles the timer; returns false if the timer was not this effect's.
  /// </summary>
  bool OnTimer(HWND timerWindow, UINT_PTR timer);
}
