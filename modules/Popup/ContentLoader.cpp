/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  ContentLoader.cpp
 *  The nModules Project
 *
 *  Reads what a content popup lists, apart from creating the items.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include "ContentLoader.hpp"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <algorithm>

namespace {
  bool Cancelled(const volatile LONG *cancel) {
    return cancel != nullptr && *cancel != 0;
  }


  /// Extracts an item's icon the way PopupItem always has: large first, then smaller,
  /// then the file's first icon, then shell32's default document icon.
  void ReadIcon(IShellFolder *folder, PCUIDLIST_RELATIVE item, ContentEntry &entry) {
    IExtractIconW *extractIcon = nullptr;
    if (FAILED(folder->GetUIObjectOf(nullptr, 1, (PCUITEMID_CHILD_ARRAY)&item, IID_IExtractIconW, nullptr,
        reinterpret_cast<LPVOID*>(&extractIcon)))) {
      return;
    }

    WCHAR iconFile[MAX_PATH];
    int iconIndex = 0;
    UINT flags;
    HRESULT hr = extractIcon->GetIconLocation(GIL_FORSHELL, iconFile, MAX_PATH, &iconIndex, &flags);
    if (SUCCEEDED(hr)) {
      entry.hasIconHash = true;
      entry.iconHash = IconCache::ComputeHash(iconFile, iconIndex);

      HICON icon = nullptr;
      hr = extractIcon->Extract(iconFile, iconIndex, &icon, nullptr, MAKELONG(64, 0));
      if (hr == S_FALSE) {
        hr = extractIcon->Extract(iconFile, iconIndex, &icon, nullptr, MAKELONG(32, 0));
      }
      if (hr == S_FALSE) {
        hr = extractIcon->Extract(iconFile, iconIndex, nullptr, &icon, MAKELONG(0, 16));
      }
      if (hr == S_FALSE) {
        icon = ExtractIconW(GetModuleHandleW(nullptr), iconFile, iconIndex);
      }
      if (!(SUCCEEDED(hr) && icon != nullptr)) {
        if (icon != nullptr) {
          DestroyIcon(icon);
        }
        icon = ExtractIconW(GetModuleHandleW(nullptr), L"shell32.dll", 1);
      }
      entry.icon = icon;
    }
    extractIcon->Release();
  }


  void ReadFromIDList(IShellFolder *folder, PIDLIST_ABSOLUTE idList, bool dontExpandFolders,
      const ContentRequest &request, ContentResult &result, const volatile LONG *cancel) {
    if (folder == nullptr || idList == nullptr) {
      return;
    }
    IEnumIDList *enumIDList = nullptr;
    if (SUCCEEDED(folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumIDList)) &&
        enumIDList != nullptr) {
      PITEMID_CHILD child = nullptr;
      while (!Cancelled(cancel) && enumIDList->Next(1, &child, nullptr) == S_OK) {
        ContentEntry entry;
        if (ContentLoader::ReadItem(folder, child, dontExpandFolders, request.noIcons, entry)) {
          result.entries.push_back(std::move(entry));
        }
        CoTaskMemFree(child);
      }
      enumIDList->Release();
      result.watch.push_back(ILCloneFull(idList));
    }
  }


  void ReadShellFolder(REFKNOWNFOLDERID id, bool dontExpandFolders, const ContentRequest &request,
      ContentResult &result, const volatile LONG *cancel) {
    PIDLIST_ABSOLUTE idList = nullptr;
    IShellFolder *desktop = nullptr, *folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(id, 0, nullptr, &idList)) && idList != nullptr &&
        SUCCEEDED(SHGetDesktopFolder(&desktop))) {
      if (SUCCEEDED(desktop->BindToObject(idList, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder)))) {
        ReadFromIDList(folder, idList, dontExpandFolders, request, result, cancel);
        folder->Release();
      }
      desktop->Release();
    }
    CoTaskMemFree(idList);
  }


  void ReadPath(LPCWSTR path, const ContentRequest &request, ContentResult &result, const volatile LONG *cancel) {
    PIDLIST_ABSOLUTE idList = nullptr;
    IShellFolder *desktop = nullptr, *folder = nullptr;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
      if (SUCCEEDED(desktop->ParseDisplayName(nullptr, nullptr, (LPWSTR)path, nullptr, &idList, nullptr)) &&
          SUCCEEDED(desktop->BindToObject(idList, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder)))) {
        ReadFromIDList(folder, idList, false, request, result, cancel);
        folder->Release();
      }
      desktop->Release();
    }
    CoTaskMemFree(idList);
  }


  /// The newest "maxRecentItems" shortcuts of the Recent folder, newest first. The folder
  /// grows without limit; Windows updates a shortcut's time each time its document opens.
  void ReadRecent(const ContentRequest &request, ContentResult &result, const volatile LONG *cancel) {
    PWSTR recentPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Recent, 0, nullptr, &recentPath)) || recentPath == nullptr) {
      return;
    }

    struct Entry { ULONGLONG time; std::wstring name; };
    std::vector<Entry> files;
    WCHAR pattern[MAX_PATH];
    StringCchPrintfW(pattern, _countof(pattern), L"%s\\*.lnk", recentPath);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
      do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
          ULONGLONG time = (ULONGLONG(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
          files.push_back({ time, data.cFileName });
        }
      } while (FindNextFileW(find, &data));
      FindClose(find);
    }
    std::sort(files.begin(), files.end(), [] (const Entry &a, const Entry &b) { return a.time > b.time; });
    size_t limit = size_t(std::max(1, request.maxRecentItems));

    IShellFolder *desktop = nullptr, *folder = nullptr;
    PIDLIST_ABSOLUTE folderId = nullptr;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
      if (SUCCEEDED(desktop->ParseDisplayName(nullptr, nullptr, recentPath, nullptr, &folderId, nullptr)) &&
          SUCCEEDED(desktop->BindToObject(folderId, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder)))) {
        // Some shortcuts cannot be resolved; keep going until the list is full.
        size_t wanted = result.entries.size() + limit;
        for (Entry &file : files) {
          if (result.entries.size() >= wanted || Cancelled(cancel)) {
            break;
          }
          PIDLIST_RELATIVE child = nullptr;
          if (SUCCEEDED(folder->ParseDisplayName(nullptr, nullptr, &file.name[0], nullptr, &child, nullptr)) && child) {
            ContentEntry entry;
            if (ContentLoader::ReadItem(folder, child, true, request.noIcons, entry)) {
              result.entries.push_back(std::move(entry));
            }
            CoTaskMemFree(child);
          }
        }
        folder->Release();
      }
      CoTaskMemFree(folderId);
      desktop->Release();
    }
    CoTaskMemFree(recentPath);
  }


  /// Every installed application, classic and packaged (the shell's Applications folder).
  void ReadApps(const ContentRequest &request, ContentResult &result, const volatile LONG *cancel) {
    PIDLIST_ABSOLUTE folderId = nullptr;
    IShellFolder *desktop = nullptr, *folder = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_AppsFolder, 0, nullptr, &folderId)) || folderId == nullptr) {
      return;
    }
    if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
      if (SUCCEEDED(desktop->BindToObject(folderId, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder)))) {
        IEnumIDList *enumerator = nullptr;
        if (folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &enumerator) == S_OK) {
          PITEMID_CHILD child = nullptr;
          while (!Cancelled(cancel) && enumerator->Next(1, &child, nullptr) == S_OK) {
            STRRET ret;
            LPWSTR name = nullptr, parsing = nullptr;
            if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_NORMAL, &ret)) &&
                SUCCEEDED(StrRetToStrW(&ret, child, &name)) &&
                SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING, &ret)) &&
                SUCCEEDED(StrRetToStrW(&ret, child, &parsing))) {
              ContentEntry entry;
              entry.name = name;
              // These entries are not files; Explorer launches them by their shell name.
              entry.command = std::wstring(Favorites::AppCommandPrefix) + parsing + L"\"";
              if (!request.noIcons) {
                PIDLIST_ABSOLUTE full = ILCombine(folderId, child);
                SHFILEINFOW info = { 0 };
                if (full != nullptr && SHGetFileInfoW(reinterpret_cast<LPCWSTR>(full), 0, &info, sizeof(info),
                    SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON)) {
                  entry.icon = info.hIcon;
                }
                ILFree(full);
              }
              result.entries.push_back(std::move(entry));
            }
            CoTaskMemFree(parsing);
            CoTaskMemFree(name);
            CoTaskMemFree(child);
          }
          enumerator->Release();
        }
        folder->Release();
      }
      desktop->Release();
    }
    CoTaskMemFree(folderId);
  }


  /// The favorites: every file in LiteStep's favorites folder.
  void ReadPinned(const ContentRequest &request, ContentResult &result, const volatile LONG *cancel) {
    WCHAR path[MAX_PATH];
    IShellFolder *desktop = nullptr;
    if (!Favorites::GetFolder(path, _countof(path)) || FAILED(SHGetDesktopFolder(&desktop))) {
      return;
    }
    PathRemoveBackslashW(path);

    PIDLIST_ABSOLUTE folderId = nullptr;
    IShellFolder *folder = nullptr;
    if (SUCCEEDED(desktop->ParseDisplayName(nullptr, nullptr, path, nullptr, &folderId, nullptr)) &&
        SUCCEEDED(desktop->BindToObject(folderId, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder)))) {
      IEnumIDList *enumerator = nullptr;
      if (folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &enumerator) == S_OK) {
        PITEMID_CHILD child = nullptr;
        while (!Cancelled(cancel) && enumerator->Next(1, &child, nullptr) == S_OK) {
          ContentEntry entry;
          if (ContentLoader::ReadItem(folder, child, true, request.noIcons, entry)) {
            result.entries.push_back(std::move(entry));
          }
          CoTaskMemFree(child);
        }
        enumerator->Release();
      }
      folder->Release();
    }
    CoTaskMemFree(folderId);
    desktop->Release();
  }
}


void ContentResult::Free() {
  for (ContentEntry &entry : this->entries) {
    if (entry.icon != nullptr) {
      DestroyIcon(entry.icon);
      entry.icon = nullptr;
    }
  }
  this->entries.clear();
  for (PIDLIST_ABSOLUTE idList : this->watch) {
    ILFree(idList);
  }
  this->watch.clear();
}


bool ContentLoader::ReadItem(IShellFolder *folder, PCUIDLIST_RELATIVE item, bool dontExpandFolders, bool noIcons,
    ContentEntry &entry) {
  STRRET ret;
  LPWSTR name = nullptr, parsing = nullptr;
  if (FAILED(folder->GetDisplayNameOf((PCUITEMID_CHILD)item, SHGDN_NORMAL, &ret)) ||
      FAILED(StrRetToStrW(&ret, nullptr, &name))) {
    return false;
  }
  if (FAILED(folder->GetDisplayNameOf((PCUITEMID_CHILD)item, SHGDN_FORPARSING, &ret)) ||
      FAILED(StrRetToStrW(&ret, nullptr, &parsing))) {
    CoTaskMemFree(name);
    return false;
  }

  SFGAOF attributes = SFGAO_BROWSABLE | SFGAO_FOLDER;
  HRESULT hr = folder->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&item, &attributes);
  entry.folder = SUCCEEDED(hr) && !dontExpandFolders &&
    ((attributes & SFGAO_FOLDER) == SFGAO_FOLDER || (attributes & SFGAO_BROWSABLE) == SFGAO_BROWSABLE);
  entry.name = name;
  entry.command = entry.folder ? std::wstring(parsing) : L"\"" + std::wstring(parsing) + L"\"";
  CoTaskMemFree(parsing);
  CoTaskMemFree(name);

  if (!noIcons) {
    ReadIcon(folder, item, entry);
  }
  return true;
}


void ContentLoader::Read(const ContentRequest &request, ContentResult &result, const volatile LONG *cancel) {
  switch (request.source) {
  case ContentPopup::ADMIN_TOOLS:
    ReadShellFolder(FOLDERID_AdminTools, false, request, result, cancel);
    ReadShellFolder(FOLDERID_CommonAdminTools, false, request, result, cancel);
    break;
  case ContentPopup::CONTROL_PANEL:
    ReadShellFolder(FOLDERID_ControlPanelFolder, true, request, result, cancel);
    break;
  case ContentPopup::MY_COMPUTER:
    ReadShellFolder(FOLDERID_ComputerFolder, false, request, result, cancel);
    break;
  case ContentPopup::NETWORK:
    ReadShellFolder(FOLDERID_NetworkFolder, false, request, result, cancel);
    break;
  case ContentPopup::PATH:
    for (const std::wstring &path : request.paths) {
      ReadPath(path.c_str(), request, result, cancel);
    }
    break;
  case ContentPopup::PRINTERS:
    ReadShellFolder(FOLDERID_PrintersFolder, false, request, result, cancel);
    break;
  case ContentPopup::RECENT_DOCUMENTS:
    ReadRecent(request, result, cancel);
    break;
  case ContentPopup::RECYCLE_BIN:
    ReadShellFolder(FOLDERID_RecycleBinFolder, false, request, result, cancel);
    break;
  case ContentPopup::START_MENU:
    ReadShellFolder(FOLDERID_StartMenu, false, request, result, cancel);
    ReadShellFolder(FOLDERID_CommonStartMenu, false, request, result, cancel);
    break;
  case ContentPopup::PROGRAMS:
    ReadShellFolder(FOLDERID_Programs, false, request, result, cancel);
    ReadShellFolder(FOLDERID_CommonPrograms, false, request, result, cancel);
    break;
  case ContentPopup::PINNED:
    ReadPinned(request, result, cancel);
    break;
  case ContentPopup::APPS:
    ReadApps(request, result, cancel);
    break;
  case ContentPopup::THEMES:
    // Read by the popup itself: a few files, and the items are plain commands.
    break;
  }
}


UINT ContentLoader::LoadedMessage() {
  static const UINT message = RegisterWindowMessageW(L"LiteStepTNG.PopupContentLoaded");
  return message;
}


namespace {
  // Workers of this module still running, and an event set while there are none.
  volatile LONG sWorkers = 0;
  HANDLE sIdle = nullptr;

  struct Job {
    HWND target;
    ContentRequest request;
    WPARAM generation;
    std::shared_ptr<volatile LONG> cancel;
    HMODULE module;
  };


  // Holds a reference to this module (taken in Start), so its code stays mapped until
  // the thread has returned, even if the module is unloaded meanwhile.
  DWORD WINAPI LoadThread(LPVOID param) {
    Job *job = reinterpret_cast<Job*>(param);
    HMODULE module = job->module;

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ContentResult *result = new ContentResult();
    ContentLoader::Read(job->request, *result, job->cancel.get());
    if (SUCCEEDED(com)) {
      CoUninitialize();
    }

    if (*job->cancel != 0 ||
        !PostMessageW(job->target, ContentLoader::LoadedMessage(), job->generation, LPARAM(result))) {
      delete result;
    }
    delete job;
    if (InterlockedDecrement(&sWorkers) == 0) {
      SetEvent(sIdle);
    }
    FreeLibraryAndExitThread(module, 0);
  }
}


bool ContentLoader::Start(HWND target, const ContentRequest &request, WPARAM generation,
    std::shared_ptr<volatile LONG> cancel) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, LPCWSTR(&LoadThread), &module)) {
    return false;
  }
  if (sIdle == nullptr) {
    sIdle = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (sIdle == nullptr) {
      FreeLibrary(module);
      return false;
    }
  }
  // Start is only called from the module's thread; workers only ever decrement.
  if (InterlockedIncrement(&sWorkers) == 1) {
    ResetEvent(sIdle);
  }
  Job *job = new Job{ target, request, generation, cancel, module };
  HANDLE thread = CreateThread(nullptr, 0, LoadThread, job, 0, nullptr);
  if (thread == nullptr) {
    delete job;
    if (InterlockedDecrement(&sWorkers) == 0) {
      SetEvent(sIdle);
    }
    FreeLibrary(module);
    return false;
  }
  CloseHandle(thread);
  return true;
}


bool ContentLoader::Shutdown(DWORD timeoutMs) {
  if (sIdle == nullptr) {
    return true;
  }
  if (WaitForSingleObject(sIdle, timeoutMs) != WAIT_OBJECT_0) {
    // Still pinned by a worker; the module stays loaded until it ends. Keep the event.
    return false;
  }
  CloseHandle(sIdle);
  sIdle = nullptr;
  return true;
}
