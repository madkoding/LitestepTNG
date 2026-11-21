/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  ContentPopup.hpp
 *  The nModules Project
 *
 *  A popup which retrives its content from outside LiteStep's .rc files.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include <strsafe.h>
#include "ContentPopup.hpp"
#include "CommandItem.hpp"
#include "FolderItem.hpp"
#include <Shlwapi.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>


ContentPopup::ContentPopup(ContentSource source, LPCTSTR title, LPCTSTR bang, LPCTSTR prefix) : Popup(title, bang, prefix)
{
    this->loaded = false;
    this->dynamic = true;
    this->source = source;
}


ContentPopup::ContentPopup(LPCTSTR path, bool dynamic, LPCTSTR title, LPCTSTR bang, LPCTSTR prefix) : Popup(title, bang, prefix)
{
    this->loaded = false;
    this->dynamic = dynamic;
    this->source = ContentSource::PATH;

    TCHAR processedPath[MAX_PATH], originalPath[MAX_PATH];
    LPCTSTR splitter, end = wcschr(path, L'\0');
    do
    {
        splitter = wcschr(path, L'|');
        StringCchCopyN(originalPath, MAX_PATH, path, (splitter != nullptr ? splitter : end) - path);
        PathCanonicalize(processedPath, originalPath);
        PathRemoveBackslash(processedPath);

        this->paths.push_back(_wcsdup(processedPath));
        path = ++splitter;
    } while (splitter != (LPCTSTR)sizeof(TCHAR));
}


ContentPopup::~ContentPopup()
{
    for (WATCHFOLDERMAP::const_iterator iter = this->watchedFolders.begin(); iter != this->watchedFolders.end(); ++iter)
    {
        iter->second.second->Release();
        mWindow->ReleaseUserMessage(iter->first);
        SHChangeNotifyDeregister(iter->second.first);
    }
    this->watchedFolders.clear();

    for (std::list<LPCTSTR>::const_iterator iter = this->paths.begin(); iter != this->paths.end(); ++iter)
    {
        free((LPVOID)*iter);
    }
    this->paths.clear();
}


void ContentPopup::PreShow()
{
    if (!this->loaded)
    {
        LoadContent();
        // Themes are already listed alphabetically by folder name; sorting by the
        // displayed title would move items around depending on the active-theme mark.
        if (this->source != ContentSource::THEMES)
        {
            std::sort(this->items.begin(), this->items.end(), [] (PopupItem* a, PopupItem* b) { return a->CompareTo(b); });
        }
        this->loaded = true;
    }
}


void ContentPopup::PostClose()
{
    if (this->dynamic)
    {
        for (WATCHFOLDERMAP::const_iterator iter = this->watchedFolders.begin(); iter != this->watchedFolders.end(); ++iter)
        {
            iter->second.second->Release();
            mWindow->ReleaseUserMessage(iter->first);
            SHChangeNotifyDeregister(iter->second.first);
        }
        this->watchedFolders.clear();

        for (vector<PopupItem*>::const_iterator iter = this->items.begin(); iter != this->items.end(); ++iter)
        {
            delete *iter;
        }
        this->items.clear();

        this->loaded = false;
    }
}


void ContentPopup::AddPath(LPCTSTR path)
{
    TCHAR processedPath[MAX_PATH];

    PathCanonicalize(processedPath, path);
    PathRemoveBackslash(processedPath);
    this->paths.push_back(_wcsdup(processedPath));

    if (this->loaded)
    {
        LoadPath(processedPath);
        std::sort(this->items.begin(), this->items.end(), [] (PopupItem* a, PopupItem* b) { return a->CompareTo(b); });
    }
}


void ContentPopup::LoadContent()
{
    switch (this->source)
    {
    case ADMIN_TOOLS:
        LoadShellFolder(FOLDERID_AdminTools);
        LoadShellFolder(FOLDERID_CommonAdminTools);
        break;

    case CONTROL_PANEL:
        LoadShellFolder(FOLDERID_ControlPanelFolder, true);
        break;

    case MY_COMPUTER:
        LoadShellFolder(FOLDERID_ComputerFolder);
        break;

    case NETWORK:
        LoadShellFolder(FOLDERID_NetworkFolder);
        break;

    case PATH:
        for (std::list<LPCTSTR>::const_iterator iter = paths.begin(); iter != paths.end(); ++iter)
        {
            LoadPath(*iter);
        }
        break;

    case PRINTERS:
        LoadShellFolder(FOLDERID_PrintersFolder);
        break;

    case RECENT_DOCUMENTS:
        LoadShellFolder(FOLDERID_Recent);
        break;

    case RECYCLE_BIN:
        LoadShellFolder(FOLDERID_RecycleBinFolder);
        break;

    case START_MENU:
        LoadShellFolder(FOLDERID_StartMenu);
        LoadShellFolder(FOLDERID_CommonStartMenu);
        break;

    case PROGRAMS:
        LoadShellFolder(FOLDERID_Programs);
        LoadShellFolder(FOLDERID_CommonPrograms);
        break;

    case THEMES:
        LoadThemes();
        break;

    case PINNED:
        LoadPinned();
        break;
    }
}


/// <summary>
/// Collects the packaged (Store) applications pinned to the taskbar.
/// Classic programs are pinned as .lnk files, but a packaged app has no shortcut file: it
/// only appears, as "PackageFamilyName!AppId", inside the binary list Explorer keeps in
/// the registry. That list has no documented layout, so we just scan it for such ids.
/// </summary>
static void ReadPinnedPackagedApps(std::vector<std::wstring> &ids) {
  static const WCHAR key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband";
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key, L"Favorites", RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS || size < 8) {
    return;
  }
  std::vector<BYTE> data(size);
  if (RegGetValueW(HKEY_CURRENT_USER, key, L"Favorites", RRF_RT_REG_BINARY, nullptr, data.data(), &size) != ERROR_SUCCESS) {
    return;
  }

  auto isIdChar = [] (WCHAR c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
      c == L'.' || c == L'_' || c == L'-';
  };

  // The strings are UTF-16 but may start at an odd byte, so try both alignments.
  for (DWORD offset = 0; offset < 2; ++offset) {
    std::wstring text((size - offset) / sizeof(WCHAR), L' ');
    memcpy(&text[0], data.data() + offset, text.size() * sizeof(WCHAR));

    for (size_t i = 0; i < text.size(); ) {
      if (!isIdChar(text[i])) {
        ++i;
        continue;
      }
      size_t start = i, bang = std::wstring::npos;
      while (i < text.size() && (isIdChar(text[i]) || (text[i] == L'!' && bang == std::wstring::npos))) {
        if (text[i] == L'!') bang = i;
        ++i;
      }
      // A family name ends in "_<publisher hash>", and something must follow the "!".
      if (bang == std::wstring::npos || bang + 1 >= i) {
        continue;
      }
      std::wstring id = text.substr(start, i - start);
      size_t underscore = id.find(L'_');
      if (underscore == std::wstring::npos || underscore > bang - start) {
        continue;
      }
      if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
        ids.push_back(id);
      }
    }
  }
}


/// <summary>
/// Lists what is pinned to the Windows taskbar: the shortcut files in the "User Pinned"
/// folder plus the packaged apps found in the registry.
/// </summary>
void ContentPopup::LoadPinned() {
  // Classic programs: one .lnk per pin. Only files; the folder also holds bookkeeping
  // subfolders that are not pins.
  WCHAR path[MAX_PATH];
  IShellFolder *desktop = nullptr;
  if (ExpandEnvironmentStringsW(L"%APPDATA%\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar",
      path, _countof(path)) && SUCCEEDED(SHGetDesktopFolder(&desktop))) {
    PIDLIST_ABSOLUTE folderId = nullptr;
    IShellFolder *folder = nullptr;
    if (SUCCEEDED(desktop->ParseDisplayName(nullptr, nullptr, path, nullptr, &folderId, nullptr)) &&
        SUCCEEDED(desktop->BindToObject(folderId, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder)))) {
      IEnumIDList *enumerator = nullptr;
      if (folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &enumerator) == S_OK) {
        PITEMID_CHILD child = nullptr;
        while (enumerator->Next(1, &child, nullptr) == S_OK) {
          LoadSingleItem(folder, child, true);
          CoTaskMemFree(child);
        }
        enumerator->Release();
      }
      folder->Release();
    }
    if (folderId != nullptr) {
      CoTaskMemFree(folderId);
    }
    desktop->Release();
  }

  // Packaged apps: resolve each id through the shell's Applications folder, which gives
  // us the display name and icon, and launch it the same way.
  std::vector<std::wstring> ids;
  ReadPinnedPackagedApps(ids);
  for (const std::wstring &id : ids) {
    WCHAR parsingName[512], command[600];
    StringCchPrintfW(parsingName, _countof(parsingName), L"shell:AppsFolder\\%s", id.c_str());

    PIDLIST_ABSOLUTE app = nullptr;
    if (FAILED(SHParseDisplayName(parsingName, nullptr, &app, 0, nullptr)) || app == nullptr) {
      continue; // pinned once, uninstalled since
    }

    SHFILEINFOW info = { 0 };
    UINT flags = SHGFI_PIDL | SHGFI_DISPLAYNAME | (this->noIcons ? 0 : (SHGFI_ICON | SHGFI_SMALLICON));
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(app), 0, &info, sizeof(info), flags) && info.szDisplayName[0] != 0) {
      StringCchPrintfW(command, _countof(command), L"explorer.exe \"%s\"", parsingName);
      if (info.hIcon != nullptr) {
        AddItem(new CommandItem(this, info.szDisplayName, command, info.hIcon));
        DestroyIcon(info.hIcon);
      } else {
        AddItem(new CommandItem(this, info.szDisplayName, command, (LPCTSTR)nullptr));
      }
    }
    CoTaskMemFree(app);
  }
}


/// <summary>
/// Builds the text shown for one theme in the !PopupThemes list.
/// </summary>
static void FormatThemeTitle(LPWSTR title, size_t cchTitle, LPCWSTR themeName, bool isCurrent)
{
    // TODO(human): mark the active theme (isCurrent) in a way that keeps every name
    // starting at the same column as the other popup items. Until then all themes are
    // listed by their bare name.
    UNREFERENCED_PARAMETER(isCurrent);
    StringCchCopyW(title, cchTitle, themeName);
}


void ContentPopup::LoadThemes()
{
    WCHAR lsDir[MAX_PATH], pattern[MAX_PATH], current[MAX_PATH];
    if (!LiteStep::LSGetLitestepPathW(lsDir, _countof(lsDir)))
    {
        return;
    }
    PathAddBackslashW(lsDir);

    // The active theme, as set by theme-select.rc.
    LiteStep::GetRCString(L"ThemeName", current, L"", _countof(current));

    StringCchPrintfW(pattern, _countof(pattern), L"%sthemes\\*", lsDir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
        {
            continue;
        }

        // Only folders that are actually themes.
        WCHAR themeRc[MAX_PATH];
        StringCchPrintfW(themeRc, _countof(themeRc), L"%sthemes\\%s\\theme.rc", lsDir, fd.cFileName);
        if (!PathFileExistsW(themeRc))
        {
            continue;
        }

        WCHAR title[MAX_PATH], command[MAX_LINE_LENGTH];
        bool isCurrent = _wcsicmp(fd.cFileName, current) == 0;
        FormatThemeTitle(title, _countof(title), fd.cFileName, isCurrent);
        StringCchPrintfW(command, _countof(command), L"!ThemeSwitch \"%s\"", fd.cFileName);
        AddItem(new CommandItem(this, title, command));
    } while (FindNextFileW(find, &fd));

    FindClose(find);
}


void ContentPopup::LoadShellFolder(GUID folder, bool dontExpandFolders)
{
    PIDLIST_ABSOLUTE idList;
    IShellFolder *targetFolder, *rootFolder;

    // Get the root IShellFolder
    SHGetDesktopFolder(reinterpret_cast<IShellFolder**>(&rootFolder));
    SHGetKnownFolderIDList(folder, NULL, NULL, &idList);
    rootFolder->BindToObject(idList, NULL, IID_IShellFolder, reinterpret_cast<LPVOID*>(&targetFolder));
    rootFolder->Release();

    //
    LoadFromIDList(targetFolder, idList, dontExpandFolders);

    if (idList != NULL)
    {
        CoTaskMemFree(idList);
    }
}


void ContentPopup::LoadPath(LPCTSTR path)
{
    PIDLIST_ABSOLUTE idList = NULL;
    IShellFolder *targetFolder, *rootFolder;

    // Get the root IShellFolder
    SHGetDesktopFolder(reinterpret_cast<IShellFolder**>(&rootFolder));
    rootFolder->ParseDisplayName(NULL, NULL, (LPTSTR)path, NULL, &idList, NULL);
    rootFolder->BindToObject(idList, NULL, IID_IShellFolder, reinterpret_cast<LPVOID*>(&targetFolder));
    rootFolder->Release();

    LoadFromIDList(targetFolder, idList, false);

    if (idList != NULL)
    {
        CoTaskMemFree(idList);
    }
}


static float time;

void ContentPopup::LoadFromIDList(IShellFolder *targetFolder, PIDLIST_ABSOLUTE idList, bool dontExpandFolders)
{
    PIDLIST_RELATIVE idNext = NULL;
    IEnumIDList* enumIDList;

    if (targetFolder == NULL || idList == NULL)
    {
        return;
    }

    time = 0.0f;

    StopWatch watch;
    // Enumerate the contents of this folder
    if (SUCCEEDED(targetFolder->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumIDList)))
    {
        while (enumIDList->Next(1, &idNext, NULL) != S_FALSE)
        {
            LoadSingleItem(targetFolder, idNext, dontExpandFolders);
            CoTaskMemFree(idNext);
        }
        enumIDList->Release();

        // Register for change notifications
        SHChangeNotifyEntry watchEntries[] = { idList, TRUE };
        UINT message = mWindow->RegisterUserMessage(this);
        ULONG shnrUID = SHChangeNotifyRegister(
            mWindow->GetWindowHandle(),
            SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
            SHCNE_CREATE | SHCNE_DELETE | SHCNE_ATTRIBUTES | SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER | SHCNE_UPDATEITEM,
            message,
            1,
            watchEntries);

        this->watchedFolders.insert(WATCHFOLDERMAP::value_type(message, std::pair<UINT, IShellFolder*>(shnrUID, targetFolder)));
    }

    TRACE("[Popup::LoadFromIDList] Total Time: %.5f, %.5f", watch.Clock(), time);

}


void ContentPopup::LoadSingleItem(IShellFolder *targetFolder, PIDLIST_RELATIVE itemID, bool dontExpandFolders)
{
    STRRET ret;
    LPTSTR name, command;
    IExtractIconW* extractIcon;
    SFGAOF attributes;
    TCHAR quotedCommand[MAX_LINE_LENGTH];
    bool openable;
    HRESULT hr;
    PopupItem* item;
    vector<PopupItem*>::const_iterator iter;

    if (SUCCEEDED(targetFolder->GetDisplayNameOf(itemID, SHGDN_NORMAL, &ret)))
    {
        StrRetToStr(&ret, NULL, &name);
        if (SUCCEEDED(targetFolder->GetDisplayNameOf(itemID, SHGDN_FORPARSING, &ret)))
        {
            StrRetToStr(&ret, NULL, &command);

            //
            attributes = SFGAO_BROWSABLE | SFGAO_FOLDER;
            hr = targetFolder->GetAttributesOf(1, (LPCITEMIDLIST *)&itemID, &attributes);
            openable = SUCCEEDED(hr) && !dontExpandFolders && (((attributes & SFGAO_FOLDER) == SFGAO_FOLDER) || ((attributes & SFGAO_BROWSABLE) == SFGAO_BROWSABLE));


            StopWatch watch;
            if (openable)
            {
                for (iter = this->items.begin(); iter != this->items.end() && !(*iter)->CheckMerge(name); ++iter);

                if (iter != this->items.end())
                {
                    item = NULL;
                    ((nPopup::FolderItem*)*iter)->AddPath(command);
                }
                else
                {
                    if (this->dynamic)
                    {
                        item = new nPopup::FolderItem(this, name, [] (nPopup::FolderItem::CreationData* data) -> Popup*
                        {
                            ContentPopup *popup = new ContentPopup(data->command, true, data->name, nullptr, data->prefix);

                            for (auto path : data->paths)
                            {
                                popup->AddPath(path);
                            }

                            return popup;
                        }, new nPopup::FolderItem::CreationData(command, name, mSettings->GetPrefix()));
                    }
                    else
                    {
                        item = new nPopup::FolderItem(this, name, new ContentPopup(command, this->dynamic, name, NULL, mSettings->GetPrefix()));
                    }
                }
            }
            else
            {
                StringCchPrintf(quotedCommand, sizeof(quotedCommand), L"\"%s\"", command);
                item = new CommandItem(this, name, quotedCommand);
            }
            time += watch.Clock();

            if (!this->noIcons && item != nullptr)
            {
                // Get the IExtractIcon interface for this item.
                hr = targetFolder->GetUIObjectOf(NULL, 1, (LPCITEMIDLIST *)&itemID, IID_IExtractIconW, nullptr, reinterpret_cast<LPVOID*>(&extractIcon));

                if (SUCCEEDED(hr))
                {
                    item->SetIcon(extractIcon);
                }
            }

            if (item != nullptr)
            {
                AddItem(item);
            }

            CoTaskMemFree(command);
        }
        CoTaskMemFree(name);
    }
}


LRESULT WINAPI ContentPopup::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID Window)
{
    if (message >= Window::WM_FIRSTREGISTERED)
    {
        WATCHFOLDERMAP::const_iterator folder = this->watchedFolders.find(message);
        if (folder != this->watchedFolders.end())
        {
            long event;
            PIDLIST_ABSOLUTE* idList;
            HANDLE notifyLock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam, &idList, &event);

            if (notifyLock)
            {
                // TODO::Have to handle deletes and renames as well
                switch (event)
                {
                case SHCNE_CREATE:
                case SHCNE_MKDIR:
                    {
                        LoadSingleItem(folder->second.second, (PIDLIST_RELATIVE)ILFindLastID(idList[0]), false);
                        std::sort(this->items.begin(), this->items.end(), [] (PopupItem* a, PopupItem* b) { return a->CompareTo(b); });
                    }
                    break;

                // A non-folder item has been renamed.
                case SHCNE_RENAMEITEM:
                    {
                        //TRACEW(L"Non-Folder renamed: %s -> %s", file1, file2);
                    }
                    break;

                // A folder has been renamed.
                case SHCNE_RENAMEFOLDER:
                    {
                        //TRACEW(L"Folder renamed: %s -> %s", file1, file2);
                    }
                    break;
                }

                SHChangeNotification_Unlock(notifyLock);
            }
            return 0;
        }
    }
    return Popup::HandleMessage(window, message, wParam, lParam, Window);
}

