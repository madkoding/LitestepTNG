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


  // Returns true when the thread should end (no subscribers left).
  bool Tick() {
    Guard guard;
    for (auto it = sSubscribers.begin(); it != sSubscribers.end();) {
      if (!IsWindow(it->first)) {
        // Gone, with or without a tick still queued: never keep a handle that may be
        // reused, and never keep the thread alive for it.
        it = sSubscribers.erase(it);
      } else if (it->second) {
        ++it;
      } else {
        // A live window whose queue refused the post (full, hung) is retried next frame.
        if (PostMessageW(it->first, FrameClock::Message(), 0, 0)) {
          it->second = true;
        }
        ++it;
      }
    }
    if (sSubscribers.empty()) {
      sRunning = false;
      return true;
    }
    return false;
  }


  // The thread holds a reference to this module (taken in Subscribe), so the code it
  // runs stays mapped until it has returned, even if Shutdown stops waiting for it.
  DWORD WINAPI ClockThread(LPVOID module) {
    for (;;) {
      // Returns right after the next composition. It fails at once while DWM is
      // not composing (locked session, some RDP sessions): keep a steady pace
      // then instead of spinning.
      if (FAILED(DwmFlush())) {
        Sleep(16);
      }

      if (Tick()) {
        FreeLibraryAndExitThread(HMODULE(module), 0);
      }
    }
  }
}


UINT FrameClock::Message() {
  static const UINT message = RegisterWindowMessageW(L"LiteStepTNG.FrameTick");
  return message;
}


UINT FrameClock::SubscriptionMessage() {
  static const UINT message = RegisterWindowMessageW(L"LiteStepTNG.FrameSubscription");
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
    HMODULE module = nullptr;
    sThread = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, LPCWSTR(&ClockThread), &module)) {
      sThread = CreateThread(nullptr, 0, ClockThread, module, 0, nullptr);
      if (!sThread) {
        FreeLibrary(module);
      }
    }
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
    // At most one frame (or the 16 ms fallback) until it sees the empty set. If DWM
    // stalls longer, the thread's own module reference keeps its code mapped.
    WaitForSingleObject(thread, 1000);
    CloseHandle(thread);
  }
}
