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
#include "ContentLoader.hpp"
#include "CommandItem.hpp"
#include "FolderItem.hpp"
#include "LoadingItem.hpp"
#include <Shlwapi.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <ShObjIdl.h>
#include <ShlGuid.h>
#include <algorithm>
#include <string>
#include <vector>


/// <summary>
/// True if the path is one of the Start Menu "Programs" folders or lies inside one.
/// Those trees hold dozens of shortcuts whose icons are slow to extract and they only
/// change when software is installed, so their popups are worth keeping in memory.
/// </summary>
static bool IsInsideProgramsFolder(LPCWSTR path) {
  static const KNOWNFOLDERID folders[] = { FOLDERID_Programs, FOLDERID_CommonPrograms };
  size_t pathLength = wcslen(path);
  for (const KNOWNFOLDERID &id : folders) {
    PWSTR root = nullptr;
    bool inside = false;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &root)) && root != nullptr) {
      size_t rootLength = wcslen(root);
      inside = pathLength >= rootLength && _wcsnicmp(path, root, rootLength) == 0 &&
        (path[rootLength] == L'\0' || path[rootLength] == L'\\');
    }
    CoTaskMemFree(root);
    if (inside) {
      return true;
    }
  }
  return false;
}


ContentPopup::ContentPopup(ContentSource source, LPCTSTR title, LPCTSTR bang, LPCTSTR prefix) : Popup(title, bang, prefix)
{
    this->loaded = false;
    this->dynamic = true;
    this->source = source;
    this->stale = false;
    this->keepLoaded = source == ContentSource::PROGRAMS || source == ContentSource::START_MENU ||
        source == ContentSource::APPS;
}


ContentPopup::ContentPopup(LPCTSTR path, bool dynamic, LPCTSTR title, LPCTSTR bang, LPCTSTR prefix) : Popup(title, bang, prefix)
{
    this->loaded = false;
    this->dynamic = dynamic;
    this->source = ContentSource::PATH;
    this->stale = false;
    // Subfolders of the Programs tree are kept like their root (decided below).
    this->keepLoaded = false;

    TCHAR processedPath[MAX_PATH], originalPath[MAX_PATH];
    LPCTSTR splitter, end = wcschr(path, L'\0');
    do
    {
        splitter = wcschr(path, L'|');
        StringCchCopyN(originalPath, MAX_PATH, path, (splitter != nullptr ? splitter : end) - path);
        PathCanonicalize(processedPath, originalPath);
        PathRemoveBackslash(processedPath);

        this->paths.push_back(_wcsdup(processedPath));
        if (IsInsideProgramsFolder(processedPath))
        {
            this->keepLoaded = true;
        }
        path = ++splitter;
    } while (splitter != (LPCTSTR)sizeof(TCHAR));
}


ContentPopup::~ContentPopup()
{
    // A worker still running posts to a window that is about to be destroyed: the post
    // fails and the worker frees its result.
    if (this->cancel)
    {
        *this->cancel = 1;
    }
    ++this->generation;

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
    if (this->loaded && this->stale)
    {
        Unload();
    }

    if (!this->loaded && !this->loading)
    {
        // Folders are read on a worker thread: reading them here would stall litestep's
        // main thread, and with it every mouse move (its low-level mouse hooks run here).
        if (this->source == ContentSource::THEMES || !StartLoading())
        {
            LoadContent();
            SortItems();
            this->loaded = true;
        }
    }
}


void ContentPopup::SortItems()
{
    // Themes are already listed alphabetically by folder name; sorting by the
    // displayed title would move items around depending on the active-theme mark.
    // Recent documents are listed newest first, not alphabetically.
    if (this->source != ContentSource::THEMES && this->source != ContentSource::RECENT_DOCUMENTS)
    {
        std::sort(this->items.begin(), this->items.end(), [] (PopupItem* a, PopupItem* b) { return a->CompareTo(b); });
    }
}


bool ContentPopup::StartLoading()
{
    std::shared_ptr<volatile LONG> stop = std::make_shared<volatile LONG>(0);
    WPARAM next = this->generation + 1;
    if (!ContentLoader::Start(mWindow->GetWindowHandle(), MakeRequest(), next, stop))
    {
        return false;
    }
    this->generation = next;
    this->cancel = stop;
    this->loading = true;
    WCHAR text[256];
    mSettings->GetString(L"LoadingText", text, _countof(text), L"Loading...");
    this->loadingRow = new LoadingItem(this, text);
    AddItem(this->loadingRow);
    return true;
}


void ContentPopup::RemoveLoadingRow()
{
    if (this->loadingRow != nullptr)
    {
        DeleteItem(this->loadingRow);
        this->loadingRow = nullptr;
    }
}


void ContentPopup::PostClose()
{
    // Closed before the content arrived: stop reading, unless the content is worth
    // keeping, in which case it is applied while hidden and the next show is instant.
    if (this->loading && !this->keepLoaded)
    {
        *this->cancel = 1;
        ++this->generation;
        this->loading = false;
        RemoveLoadingRow();
    }

    // Dynamic content is rebuilt on every show, except what is worth keeping: rebuilding
    // the Programs tree means re-extracting every icon, which is the delay users notice.
    if (this->dynamic && (!this->keepLoaded || this->stale))
    {
        Unload();
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
        ContentRequest request = MakeRequest();
        request.source = ContentSource::PATH;
        request.paths.assign(1, processedPath);
        ContentResult result;
        ContentLoader::Read(request, result, nullptr);
        CreateItems(result);
        std::sort(this->items.begin(), this->items.end(), [] (PopupItem* a, PopupItem* b) { return a->CompareTo(b); });
    }
}


void ContentPopup::LoadContent()
{
    if (this->source == ContentSource::THEMES)
    {
        LoadThemes();
        return;
    }
    ContentResult result;
    ContentLoader::Read(MakeRequest(), result, nullptr);
    CreateItems(result);
}



/// <summary>
/// Frees the items and stops watching the folders; the next show loads again.
/// </summary>
void ContentPopup::Unload() {
  if (this->loading)
  {
    *this->cancel = 1;
    ++this->generation;
    this->loading = false;
  }
  // Deleted with the other items below.
  this->loadingRow = nullptr;

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
  this->stale = false;
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
/// Saves a shortcut to a shell item (used for packaged apps, which have no file to copy).
/// </summary>
static bool SaveShortcutToItem(PCIDLIST_ABSOLUTE item, LPCWSTR shortcutFile) {
  IShellLinkW *link = nullptr;
  bool saved = false;
  if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
    IPersistFile *file = nullptr;
    if (SUCCEEDED(link->SetIDList(item)) && SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
      saved = SUCCEEDED(file->Save(shortcutFile, TRUE));
      file->Release();
    }
    link->Release();
  }
  return saved;
}


/// <summary>
/// Fills a new favorites folder with what is pinned to the Windows taskbar today: the
/// shortcut files of the classic programs, plus a shortcut made for each packaged app.
/// </summary>
static void SeedFavorites(LPCWSTR favorites) {
  WCHAR pinned[MAX_PATH], pattern[MAX_PATH], source[MAX_PATH], target[MAX_PATH];
  if (ExpandEnvironmentStringsW(L"%APPDATA%\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\",
      pinned, _countof(pinned))) {
    StringCchPrintfW(pattern, _countof(pattern), L"%s*.lnk", pinned);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
      do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
          StringCchPrintfW(source, _countof(source), L"%s%s", pinned, data.cFileName);
          StringCchPrintfW(target, _countof(target), L"%s%s", favorites, data.cFileName);
          CopyFileW(source, target, TRUE);
        }
      } while (FindNextFileW(find, &data));
      FindClose(find);
    }
  }

  std::vector<std::wstring> ids;
  ReadPinnedPackagedApps(ids);
  for (const std::wstring &id : ids) {
    WCHAR parsingName[512];
    StringCchPrintfW(parsingName, _countof(parsingName), L"shell:AppsFolder\\%s", id.c_str());
    PIDLIST_ABSOLUTE app = nullptr;
    if (FAILED(SHParseDisplayName(parsingName, nullptr, &app, 0, nullptr)) || app == nullptr) {
      continue;
    }
    SHFILEINFOW info = { 0 };
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(app), 0, &info, sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME) &&
        info.szDisplayName[0] != 0) {
      // Display names can hold characters a file name cannot.
      for (WCHAR *c = info.szDisplayName; *c; ++c) {
        if (wcschr(L"\\/:*?\"<>|", *c)) *c = L' ';
      }
      StringCchPrintfW(target, _countof(target), L"%s%s.lnk", favorites, info.szDisplayName);
      if (!PathFileExistsW(target)) {
        SaveShortcutToItem(app, target);
      }
    }
    CoTaskMemFree(app);
  }
}


bool Favorites::GetFolder(LPWSTR path, size_t cchPath) {
  WCHAR lsDir[MAX_PATH];
  if (!LiteStep::LSGetLitestepPathW(lsDir, _countof(lsDir))) {
    return false;
  }
  PathAddBackslashW(lsDir);
  if (FAILED(StringCchPrintfW(path, cchPath, L"%sfavorites\\", lsDir))) {
    return false;
  }
  if (!PathIsDirectoryW(path)) {
    if (!CreateDirectoryW(path, nullptr)) {
      return false;
    }
    SeedFavorites(path);
  }
  return true;
}


bool Favorites::Contains(LPCWSTR file) {
  WCHAR folder[MAX_PATH];
  if (!GetFolder(folder, _countof(folder))) {
    return false;
  }
  size_t length = wcslen(folder);
  return _wcsnicmp(file, folder, length) == 0 && wcschr(file + length, L'\\') == nullptr;
}


bool Favorites::Add(LPCWSTR file) {
  WCHAR folder[MAX_PATH], target[MAX_PATH];
  if (!GetFolder(folder, _countof(folder)) ||
      FAILED(StringCchPrintfW(target, _countof(target), L"%s%s", folder, PathFindFileNameW(file)))) {
    return false;
  }
  return CopyFileW(file, target, FALSE) != FALSE;
}


const WCHAR Favorites::AppCommandPrefix[] = L"explorer.exe \"shell:AppsFolder\\";


bool Favorites::AddApp(LPCWSTR appParsingName, LPCWSTR title) {
  WCHAR folder[MAX_PATH], name[MAX_PATH], target[MAX_PATH], parsingName[1024];
  if (!GetFolder(folder, _countof(folder)) ||
      FAILED(StringCchCopyW(name, _countof(name), title)) ||
      FAILED(StringCchPrintfW(parsingName, _countof(parsingName), L"shell:AppsFolder\\%s", appParsingName))) {
    return false;
  }
  // Titles can hold characters a file name cannot.
  for (WCHAR *c = name; *c; ++c) {
    if (wcschr(L"\\/:*?\"<>|", *c)) *c = L' ';
  }
  if (FAILED(StringCchPrintfW(target, _countof(target), L"%s%s.lnk", folder, name))) {
    return false;
  }

  PIDLIST_ABSOLUTE app = nullptr;
  if (FAILED(SHParseDisplayName(parsingName, nullptr, &app, 0, nullptr)) || app == nullptr) {
    return false;
  }
  bool saved = SaveShortcutToItem(app, target);
  CoTaskMemFree(app);
  return saved;
}


bool Favorites::Remove(LPCWSTR file) {
  return Contains(file) && DeleteFileW(file) != FALSE;
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


/// <summary>
/// What to read for this popup; a copy, so a worker thread never touches the popup.
/// </summary>
ContentRequest ContentPopup::MakeRequest() const
{
    ContentRequest request;
    request.source = this->source;
    for (LPCTSTR path : this->paths)
    {
        request.paths.push_back(path);
    }
    request.maxRecentItems = mSettings->GetInt(L"MaxRecentItems", 20);
    request.noIcons = this->noIcons;
    return request;
}


/// <summary>
/// Creates the items of a load (merging folders of the same name, as the user and common
/// halves of the Start Menu need) and starts watching its folders. Takes the icons and ID
/// lists out of the result.
/// </summary>
void ContentPopup::CreateItems(ContentResult &result)
{
    for (ContentEntry &entry : result.entries)
    {
        PopupItem *item = nullptr;
        if (entry.folder)
        {
            vector<PopupItem*>::const_iterator iter;
            for (iter = this->items.begin(); iter != this->items.end() && !(*iter)->CheckMerge(entry.name.c_str()); ++iter);

            if (iter != this->items.end())
            {
                ((nPopup::FolderItem*)*iter)->AddPath(entry.command.c_str());
            }
            else if (this->dynamic)
            {
                item = new nPopup::FolderItem(this, entry.name.c_str(), [] (nPopup::FolderItem::CreationData* data) -> Popup*
                {
                    ContentPopup *popup = new ContentPopup(data->command, true, data->name, nullptr, data->prefix);

                    for (auto path : data->paths)
                    {
                        popup->AddPath(path);
                    }

                    return popup;
                }, new nPopup::FolderItem::CreationData(entry.command.c_str(), entry.name.c_str(), mSettings->GetPrefix()));
            }
            else
            {
                item = new nPopup::FolderItem(this, entry.name.c_str(),
                    new ContentPopup(entry.command.c_str(), this->dynamic, entry.name.c_str(), NULL, mSettings->GetPrefix()));
            }
        }
        else if (entry.icon != nullptr && !entry.hasIconHash)
        {
            // An icon with no location to share it by (Applications folder): the item
            // keeps its own copy.
            item = new CommandItem(this, entry.name.c_str(), entry.command.c_str(), entry.icon);
            DestroyIcon(entry.icon);
            entry.icon = nullptr;
        }
        else
        {
            item = new CommandItem(this, entry.name.c_str(), entry.command.c_str(), (LPCTSTR)nullptr);
        }

        if (entry.icon != nullptr)
        {
            if (item != nullptr)
            {
                item->SetIcon(entry.icon, entry.iconHash);
            }
            else
            {
                DestroyIcon(entry.icon);
            }
            entry.icon = nullptr;
        }

        if (item != nullptr)
        {
            AddItem(item);
        }
    }
    result.entries.clear();

    // Watch the folders from this thread: a worker's shell objects cannot be used here.
    IShellFolder *desktop = nullptr;
    if (!result.watch.empty() && SUCCEEDED(SHGetDesktopFolder(&desktop)))
    {
        for (PIDLIST_ABSOLUTE idList : result.watch)
        {
            IShellFolder *folder = nullptr;
            if (SUCCEEDED(desktop->BindToObject(idList, nullptr, IID_IShellFolder, reinterpret_cast<LPVOID*>(&folder))))
            {
                SHChangeNotifyEntry watchEntries[] = { idList, TRUE };
                UINT message = mWindow->RegisterUserMessage(this);
                ULONG shnrUID = SHChangeNotifyRegister(
                    mWindow->GetWindowHandle(),
                    SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
                    SHCNE_CREATE | SHCNE_DELETE | SHCNE_ATTRIBUTES | SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER | SHCNE_UPDATEITEM,
                    message,
                    1,
                    watchEntries);
                this->watchedFolders.insert(WATCHFOLDERMAP::value_type(message, std::pair<UINT, IShellFolder*>(shnrUID, folder)));
            }
        }
        desktop->Release();
    }
    for (PIDLIST_ABSOLUTE idList : result.watch)
    {
        ILFree(idList);
    }
    result.watch.clear();
}




LRESULT WINAPI ContentPopup::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID Window)
{
    if (message == ContentLoader::LoadedMessage())
    {
        std::unique_ptr<ContentResult> result(reinterpret_cast<ContentResult*>(lParam));
        if (this->loading && wParam == this->generation)
        {
            // The popup is visible: without the lock every item added, moved and sized
            // would repaint it synchronously (about a second for 20 items). One paint at
            // the end instead.
            Window::UpdateLock lock(mWindow);
            RemoveLoadingRow();
            CreateItems(*result);
            SortItems();
            this->loading = false;
            this->loaded = true;
            Refit();
        }
        return 0;
    }

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
                        ContentResult created;
                        created.entries.emplace_back();
                        if (ContentLoader::ReadItem(folder->second.second, ILFindLastID(idList[0]), false,
                            this->noIcons, created.entries.back()))
                        {
                            CreateItems(created);
                        }
                        std::sort(this->items.begin(), this->items.end(), [] (PopupItem* a, PopupItem* b) { return a->CompareTo(b); });
                    }
                    break;

                // Something was removed or renamed. We do not patch that in place: a popup
                // that is being kept is marked stale and rebuilt on its next show.
                case SHCNE_DELETE:
                case SHCNE_RMDIR:
                    this->stale = true;
                    break;

                // A non-folder item has been renamed.
                case SHCNE_RENAMEITEM:
                    this->stale = true;
                    {
                        //TRACEW(L"Non-Folder renamed: %s -> %s", file1, file2);
                    }
                    break;

                // A folder has been renamed.
                case SHCNE_RENAMEFOLDER:
                    this->stale = true;
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

