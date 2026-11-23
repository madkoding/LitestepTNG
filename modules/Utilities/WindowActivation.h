//-------------------------------------------------------------------------------------------------
// /Utilities/WindowActivation.h
// The nModules Project
//
// Giving the foreground to a window from a module.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <Windows.h>

namespace WindowActivation {
  /// <summary>
  /// Gives a window the foreground, even when Windows would refuse a plain request.
  ///
  /// The shell's own windows never take focus (WS_EX_NOACTIVATE), and a bang run from a
  /// timer or a keyboard hook has not just received input, so SetForegroundWindow alone
  /// is often refused. Attaching to the input queue of whoever has the foreground lends
  /// this thread the right to change it.
  /// </summary>
  inline void ForceForeground(HWND window) {
    if (SetForegroundWindow(window) && GetForegroundWindow() == window) {
      return;
    }
    DWORD self = GetCurrentThreadId();
    DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    if (foreground != 0 && foreground != self && AttachThreadInput(self, foreground, TRUE)) {
      BringWindowToTop(window);
      SetForegroundWindow(window);
      AttachThreadInput(self, foreground, FALSE);
    }
  }
}
