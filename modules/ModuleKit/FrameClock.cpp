/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  FrameClock.cpp
 *  The nModules Project
 *
 *  A tick paced by the display refresh, for animations.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "FrameClock.hpp"

#include <dwmapi.h>
#include <map>

#pragma comment(lib, "dwmapi.lib")

namespace {
  SRWLOCK sLock = SRWLOCK_INIT;
  // Subscribed windows and whether a tick is already queued for each.
  std::map<HWND, bool> sSubscribers;
  HANDLE sThread = nullptr;
  bool sRunning = false;

  class Guard {
  public:
    Guard() { AcquireSRWLockExclusive(&sLock); }
    ~Guard() { ReleaseSRWLockExclusive(&sLock); }
  };


  DWORD WINAPI ClockThread(LPVOID) {
    for (;;) {
      // Returns right after the next composition. It fails at once while DWM is
      // not composing (locked session, some RDP sessions): keep a steady pace
      // then instead of spinning.
      if (FAILED(DwmFlush())) {
        Sleep(16);
      }

      Guard guard;
      for (auto it = sSubscribers.begin(); it != sSubscribers.end();) {
        if (it->second) {
          ++it;
        } else if (PostMessageW(it->first, FrameClock::Message(), 0, 0)) {
          it->second = true;
          ++it;
        } else {
          // The window is gone: never keep posting to a handle that may be reused.
          it = sSubscribers.erase(it);
        }
      }
      if (sSubscribers.empty()) {
        sRunning = false;
        return 0;
      }
    }
  }
}


UINT FrameClock::Message() {
  static const UINT message = RegisterWindowMessageW(L"LiteStepTNG.FrameTick");
  return message;
}


void FrameClock::Subscribe(HWND window) {
  Guard guard;
  sSubscribers.emplace(window, false);
  if (!sRunning) {
    // A previous thread has already left its loop; it no longer touches our state.
    if (sThread) {
      CloseHandle(sThread);
    }
    sThread = CreateThread(nullptr, 0, ClockThread, nullptr, 0, nullptr);
    sRunning = sThread != nullptr;
  }
}


void FrameClock::Unsubscribe(HWND window) {
  Guard guard;
  sSubscribers.erase(window);
}


void FrameClock::Ack(HWND window) {
  Guard guard;
  auto it = sSubscribers.find(window);
  if (it != sSubscribers.end()) {
    it->second = false;
  }
}


void FrameClock::Shutdown() {
  HANDLE thread;
  {
    Guard guard;
    sSubscribers.clear();
    thread = sThread;
    sThread = nullptr;
  }
  if (thread) {
    // At most one frame (or the 16 ms fallback) until it sees the empty set.
    WaitForSingleObject(thread, 1000);
    CloseHandle(thread);
  }
}
