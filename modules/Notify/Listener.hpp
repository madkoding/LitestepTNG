//-------------------------------------------------------------------------------------------------
// /Notify/Listener.hpp
// The nModules Project
//
// Reads the Windows toast notifications on a background thread.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

struct ToastData {
  // The id Windows gave the notification; ids made up by !NotifyTest have the top bit set.
  UINT32 id;
  std::wstring app;
  // The app's AppUserModelID; empty for !NotifyTest boxes.
  std::wstring appId;
  std::wstring title;
  std::wstring body;
  // When the notification was raised (UTC).
  FILETIME time;
};

namespace Listener {
  /// <summary>
  /// Starts watching. "message" is posted to "window" whenever Drain has something new.
  /// </summary>
  /// <param name="showExisting">True to also report what is already in the notification
  /// centre when the module loads.</param>
  void Start(HWND window, UINT message, UINT pollInterval, bool showExisting);
  void Stop();

  /// <summary>
  /// Takes what arrived (oldest first) and the ids Windows no longer has.
  /// </summary>
  void Drain(std::vector<ToastData> &added, std::vector<UINT32> &removed);

  /// <summary>
  /// Removes a notification from the Windows notification centre.
  /// </summary>
  void Dismiss(UINT32 id);
}
