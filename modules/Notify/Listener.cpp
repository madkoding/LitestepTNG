//-------------------------------------------------------------------------------------------------
// /Notify/Listener.cpp
// The nModules Project
//
// Reads the Windows toast notifications on a background thread.
//
// UserNotificationListener has a NotificationChanged event, but it only works for packaged
// apps, so the list is polled and compared against the previous one.
//-------------------------------------------------------------------------------------------------
#include "Listener.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#pragma comment(lib, "windowsapp.lib")

using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::UI::Notifications::Management;

static std::mutex sMutex;
static std::condition_variable sWake;
static std::vector<ToastData> sAdded; // guarded by sMutex
static std::vector<UINT32> sRemoved;         // guarded by sMutex
static std::vector<UINT32> sDismiss;         // guarded by sMutex
static bool sRun = false;                    // guarded by sMutex
static std::thread sThread;

static HWND sWindow;
static UINT sMessage;
static UINT sPollInterval;
static bool sShowExisting;


/// <summary>
/// Copies what the boxes show out of a WinRT notification. Every part is optional: an app
/// can be gone by the time its notification is read, and a toast may have no text at all.
/// </summary>
static ToastData Read(const UserNotification &notification) {
  ToastData data = { notification.Id() };
  data.time = winrt::clock::to_FILETIME(notification.CreationTime());

  try {
    data.appId = notification.AppInfo().AppUserModelId().c_str();
    data.app = notification.AppInfo().DisplayInfo().DisplayName().c_str();
  } catch (...) {}

  try {
    auto binding = notification.Notification().Visual().GetBinding(
      KnownNotificationBindings::ToastGeneric());
    if (binding) {
      bool first = true;
      for (auto const &element : binding.GetTextElements()) {
        std::wstring text = element.Text().c_str();
        if (first) {
          data.title = text;
          first = false;
        } else if (!text.empty()) {
          if (!data.body.empty()) {
            data.body += L"\n";
          }
          data.body += text;
        }
      }
    }
  } catch (...) {}

  return data;
}


static void Loop() {
  try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}

  UserNotificationListener listener{ nullptr };
  std::set<UINT32> known;
  bool first = true;

  for (;;) {
    std::vector<UINT32> dismiss;
    {
      std::lock_guard<std::mutex> lock(sMutex);
      if (!sRun) {
        break;
      }
      dismiss.swap(sDismiss);
    }

    try {
      if (!listener) {
        listener = UserNotificationListener::Current();
        if (listener.GetAccessStatus() != UserNotificationListenerAccessStatus::Allowed) {
          listener.RequestAccessAsync().get();
        }
      }

      for (UINT32 id : dismiss) {
        try { listener.RemoveNotification(id); } catch (...) {}
        // The box is already gone; do not report this one back as removed.
        known.erase(id);
      }

      std::vector<ToastData> added;
      std::vector<UINT32> removed;
      std::set<UINT32> current;
      for (auto const &notification : listener.GetNotificationsAsync(NotificationKinds::Toast).get()) {
        UINT32 id = notification.Id();
        current.insert(id);
        if (known.find(id) == known.end() && (!first || sShowExisting)) {
          added.push_back(Read(notification));
        }
      }
      for (UINT32 id : known) {
        if (current.find(id) == current.end()) {
          removed.push_back(id);
        }
      }
      known.swap(current);
      first = false;

      if (!added.empty() || !removed.empty()) {
        std::sort(added.begin(), added.end(), [] (const ToastData &a, const ToastData &b) {
          return CompareFileTime(&a.time, &b.time) < 0;
        });
        {
          std::lock_guard<std::mutex> lock(sMutex);
          sAdded.insert(sAdded.end(), added.begin(), added.end());
          sRemoved.insert(sRemoved.end(), removed.begin(), removed.end());
        }
        PostMessageW(sWindow, sMessage, 0, 0);
      }
    } catch (...) {
      // Access denied or the platform is not ready yet; try again on the next round.
      listener = nullptr;
    }

    std::unique_lock<std::mutex> lock(sMutex);
    sWake.wait_for(lock, std::chrono::milliseconds(sPollInterval), [] {
      return !sRun || !sDismiss.empty();
    });
  }

  listener = nullptr;
  winrt::uninit_apartment();
}


void Listener::Start(HWND window, UINT message, UINT pollInterval, bool showExisting) {
  Stop();
  sWindow = window;
  sMessage = message;
  sPollInterval = (std::max)(pollInterval, 250u);
  sShowExisting = showExisting;
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sRun = true;
  }
  sThread = std::thread(Loop);
}


void Listener::Stop() {
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sRun = false;
  }
  sWake.notify_all();
  if (sThread.joinable()) {
    sThread.join();
  }
  std::lock_guard<std::mutex> lock(sMutex);
  sAdded.clear();
  sRemoved.clear();
  sDismiss.clear();
}


void Listener::Drain(std::vector<ToastData> &added, std::vector<UINT32> &removed) {
  std::lock_guard<std::mutex> lock(sMutex);
  added.swap(sAdded);
  removed.swap(sRemoved);
  sAdded.clear();
  sRemoved.clear();
}


void Listener::Dismiss(UINT32 id) {
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sDismiss.push_back(id);
  }
  sWake.notify_all();
}
