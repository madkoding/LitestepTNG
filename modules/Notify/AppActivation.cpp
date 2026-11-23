//-------------------------------------------------------------------------------------------------
// /Notify/AppActivation.cpp
// The nModules Project
//
// Takes the user to the program a notification came from.
//
// Windows does not let another program "click" a toast, so the notification's own action
// (open this chat, this mail) is out of reach. What a notification does carry is the
// AppUserModelID of its app, which is enough to find the app's window or to start it.
//-------------------------------------------------------------------------------------------------
#include "AppActivation.hpp"

#include <appmodel.h>
#include <dwmapi.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "propsys.lib")

struct Search {
  LPCWSTR appId;
  HWND found;
};


/// <summary>
/// The AppUserModelID a window is grouped under: the one set on the window itself, or
/// else its process's package identity. Apps that set an explicit id on their process
/// only (most Electron apps) cannot be read from outside and return false.
/// </summary>
static bool GetWindowAppId(HWND window, std::wstring &appId) {
  IPropertyStore *store = nullptr;
  if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) {
    PROPVARIANT value;
    PropVariantInit(&value);
    bool found = SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value))
      && value.vt == VT_LPWSTR && value.pwszVal != nullptr && *value.pwszVal != L'\0';
    if (found) {
      appId = value.pwszVal;
    }
    PropVariantClear(&value);
    store->Release();
    if (found) {
      return true;
    }
  }

  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (process == nullptr) {
    return false;
  }
  WCHAR buffer[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
  UINT32 length = _countof(buffer);
  bool found = GetApplicationUserModelId(process, &length, buffer) == ERROR_SUCCESS;
  CloseHandle(process);
  if (found) {
    appId = buffer;
  }
  return found;
}


static BOOL CALLBACK FindProc(HWND window, LPARAM lParam) {
  Search *search = (Search *)lParam;

  // Only what a taskbar would list: visible, unowned, not a tool window, and not one of
  // the cloaked frames Windows keeps around for suspended Store apps.
  if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr
      || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
    return TRUE;
  }
  int cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
    return TRUE;
  }

  std::wstring appId;
  if (GetWindowAppId(window, appId) && _wcsicmp(appId.c_str(), search->appId) == 0) {
    search->found = window;
    return FALSE;
  }
  return TRUE;
}


/// <summary>
/// Gives a window the foreground. The boxes never take focus themselves
/// (WS_EX_NOACTIVATE), so Windows may refuse a plain SetForegroundWindow; borrowing the
/// input queue of whoever has the foreground gets around that, as the popups do.
/// </summary>
static void BringToFront(HWND window) {
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


bool AppActivation::Activate(LPCWSTR appId) {
  if (appId == nullptr || *appId == L'\0') {
    return false;
  }

  Search search = { appId, nullptr };
  EnumWindows(FindProc, (LPARAM)&search);
  if (search.found != nullptr) {
    if (IsIconic(search.found)) {
      ShowWindow(search.found, SW_RESTORE);
    }
    BringToFront(search.found);
    return true;
  }

  // No window we can recognise: start the app. One that is already running (in the tray,
  // or with an id we could not read) answers this by showing its existing window.
  std::wstring target = std::wstring(L"shell:AppsFolder\\") + appId;
  return (INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", target.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}
