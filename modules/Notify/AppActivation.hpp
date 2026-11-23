//-------------------------------------------------------------------------------------------------
// /Notify/AppActivation.hpp
// The nModules Project
//
// Takes the user to the program a notification came from.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <Windows.h>

namespace AppActivation {
  /// <summary>
  /// Brings a window of the app to the front, or starts the app if it has none.
  /// </summary>
  /// <param name="appId">The app's AppUserModelID.</param>
  /// <returns>False if there was nothing to activate or launch.</returns>
  bool Activate(LPCWSTR appId);
}
