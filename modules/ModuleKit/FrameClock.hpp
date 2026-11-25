/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  FrameClock.hpp
 *  The nModules Project
 *
 *  A tick paced by the display refresh, for animations.
 *
 *  SetTimer is rounded to the system tick (~15.6 ms) and is not aligned with
 *  the monitor, so animations driven by it step unevenly. While at least one
 *  window is subscribed, a worker thread waits for each composition with
 *  DwmFlush() and posts Message() to every subscribed window. A window has at
 *  most one tick queued at a time: it must call Ack() when it handles one, so
 *  a busy UI thread skips frames instead of piling them up. With no
 *  subscribers there is no thread and nothing is posted.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include "../Utilities/Common.h"

namespace FrameClock {
  /// The registered message posted once per frame. The same in every module.
  UINT Message();

  /// Starts sending ticks to the window. Subscribing twice has no effect.
  void Subscribe(HWND window);

  /// Stops sending ticks to the window. Safe for windows that are not subscribed.
  void Unsubscribe(HWND window);

  /// Marks the window's queued tick as handled, so the next frame posts another.
  void Ack(HWND window);

  /// Drops every subscriber and waits for the thread to end. Call on module unload.
  void Shutdown();
}
