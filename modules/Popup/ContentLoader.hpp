/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  ContentLoader.hpp
 *  The nModules Project
 *
 *  Reads what a content popup lists (folders, recent documents, applications)
 *  into plain records, apart from creating the items.
 *
 *  Popup items are windows and must be created on litestep's main thread, but
 *  enumerating a folder and extracting its icons is what takes time. Reading
 *  needs no window, so it can run on a worker thread: only strings, icons and
 *  item ID lists come out of it, never a COM object.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include "ContentPopup.hpp"
#include "IconCache.hpp"

#include <memory>
#include <string>
#include <vector>

/// One item of a content popup, read but not created.
struct ContentEntry {
  std::wstring name;
  // The shell's parsing name for a folder; the command to run (quoted path, or an
  // Applications-folder launch) for anything else.
  std::wstring command;
  // Opens as a submenu (FolderItem) instead of running (CommandItem).
  bool folder = false;
  // Owned by the entry until it is handed to an item; nullptr when there is none.
  HICON icon = nullptr;
  // Set when the icon has a location (IExtractIcon), so items can share it through the
  // icon cache.
  bool hasIconHash = false;
  IconCache::Hash iconHash = 0;
};

/// What to read; a copy, so the worker never touches the popup.
struct ContentRequest {
  ContentPopup::ContentSource source = ContentPopup::ContentSource::PATH;
  std::vector<std::wstring> paths;
  int maxRecentItems = 20;
  bool noIcons = false;
};

/// The records of one load.
struct ContentResult {
  std::vector<ContentEntry> entries;
  // Folders to watch for changes, as absolute ID lists (ILCloneFull); only folder
  // sources (paths and shell folders) have them.
  std::vector<PIDLIST_ABSOLUTE> watch;

  ~ContentResult() { Free(); }
  // Destroys every icon and frees every ID list still held.
  void Free();
};

namespace ContentLoader {
  /// Reads every item of the request. Stops early, with what it has, when *cancel
  /// becomes nonzero. Needs COM initialized on the calling thread.
  void Read(const ContentRequest &request, ContentResult &result, const volatile LONG *cancel);

  /// Reads one item of a shell folder. Returns false if it has no name.
  bool ReadItem(IShellFolder *folder, PCUIDLIST_RELATIVE item, bool dontExpandFolders, bool noIcons,
    ContentEntry &entry);

  /// The registered message a worker posts to the popup's window when it is done.
  /// wParam: the load's generation; lParam: ContentResult*, owned by the receiver.
  UINT LoadedMessage();

  /// Reads the request on a new thread and posts LoadedMessage() to target. If the
  /// post fails (the window is gone) the result is freed by the thread. The thread
  /// keeps this module loaded until it ends. Returns false if no thread could start.
  bool Start(HWND target, const ContentRequest &request, WPARAM generation,
    std::shared_ptr<volatile LONG> cancel);
}
